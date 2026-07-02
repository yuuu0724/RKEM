#include "sort_cycle_controller.h"

#include <algorithm>
#include <cstdio>

namespace {
std::string slotText(const ChipSortResult& result)
{
    return "槽位" + std::to_string(result.slot_index) + (result.good ? "良品" : "次品");
}
}

SortCycleController::SortCycleController(std::unique_ptr<ServoDriver> servo)
    : servo_(std::move(servo))
{
}

SortCycleController::~SortCycleController()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (servo_) {
            servo_->stopAll();
        }
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SortCycleController::setCallbacks(const SortCycleCallbacks& callbacks)
{
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_ = callbacks;
}

void SortCycleController::configure(const SortCycleConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    config_.chip_count = std::max(1, config_.chip_count);
    config_.chip_spacing_mm = std::max(1, config_.chip_spacing_mm);
    config_.first_chip_to_sort_area_mm = std::max(1, config_.first_chip_to_sort_area_mm);
}

bool SortCycleController::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!servo_) {
        return false;
    }
    const bool ok = servo_->init();
    setStatusLocked(ok ? "机械臂舵机初始化完成" : "机械臂舵机初始化失败，分拣动作不可用");
    return ok;
}

void SortCycleController::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    joinWorkerIfFinishedLocked();
    results_.clear();
    current_sort_index_ = 0;
    state_ = State::Inspecting;
    setStatusLocked("分拣控制器已复位，等待4块芯片识别结果");
}

bool SortCycleController::recordInspectionResult(const ChipSortResult& result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    joinWorkerIfFinishedLocked();
    if (state_ == State::Idle || state_ == State::Completed || state_ == State::Error) {
        state_ = State::Inspecting;
        results_.clear();
        current_sort_index_ = 0;
    }
    if (state_ != State::Inspecting) {
        setStatusLocked("当前分拣流程未处于识别阶段，忽略新的芯片结果");
        return false;
    }

    results_.push_back(result);
    setStatusLocked("已保存" + slotText(result) + "结果 " +
                    std::to_string(results_.size()) + "/" + std::to_string(config_.chip_count));
    if (static_cast<int>(results_.size()) < config_.chip_count) {
        return true;
    }

    const int bridge_mm = bridgeDistanceMmLocked();
    if (bridge_mm < 0) {
        state_ = State::Error;
        setStatusLocked("分拣参数错误：第一块到分拣区距离小于识别阶段累计前进距离");
        return false;
    }
    current_sort_index_ = 0;
    if (bridge_mm == 0) {
        startSortingLocked();
        return true;
    }
    return requestForwardLocked(bridge_mm, "首片移动到分拣区", State::WaitingFirstToSortArea);
}

void SortCycleController::onTrayMotionDone(TrayMotionKind kind)
{
    std::lock_guard<std::mutex> lock(mutex_);
    joinWorkerIfFinishedLocked();
    if (kind == TrayMotionKind::Forward) {
        if (state_ == State::WaitingFirstToSortArea || state_ == State::WaitingNextChip) {
            startSortingLocked();
        }
        return;
    }

    if (state_ == State::WaitingReturnHome) {
        state_ = State::Completed;
        setStatusLocked("四块芯片分拣完成，料盘已回到初始位置");
        if (callbacks_.completed) {
            callbacks_.completed();
        }
    }
}

void SortCycleController::onTrayMotionInterrupted()
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Error;
    setStatusLocked("分拣流程被下位机中断");
}

void SortCycleController::setStatusLocked(const std::string& message)
{
    std::fprintf(stdout, "[SORT] %s\n", message.c_str());
    std::fflush(stdout);
    if (callbacks_.status) {
        callbacks_.status(message);
    }
}

bool SortCycleController::requestForwardLocked(int distance_mm,
                                               const std::string& label,
                                               State wait_state)
{
    if (distance_mm <= 0) {
        return true;
    }
    if (!callbacks_.move_forward_mm || !callbacks_.move_forward_mm(distance_mm, label)) {
        state_ = State::Error;
        setStatusLocked(label + "前进命令发送失败");
        return false;
    }
    state_ = wait_state;
    setStatusLocked(label + "：料盘前进" + std::to_string(distance_mm) + "mm");
    return true;
}

bool SortCycleController::requestBackwardLocked(int distance_mm,
                                                const std::string& label,
                                                State wait_state)
{
    if (distance_mm <= 0) {
        state_ = State::Completed;
        return true;
    }
    if (!callbacks_.move_backward_mm || !callbacks_.move_backward_mm(distance_mm, label)) {
        state_ = State::Error;
        setStatusLocked(label + "回退命令发送失败");
        return false;
    }
    state_ = wait_state;
    setStatusLocked(label + "：料盘回退" + std::to_string(distance_mm) + "mm");
    return true;
}

void SortCycleController::startSortingLocked()
{
    if (current_sort_index_ < 0 || current_sort_index_ >= static_cast<int>(results_.size())) {
        state_ = State::Error;
        setStatusLocked("分拣索引越界");
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    const ChipSortResult result = results_.at(current_sort_index_);
    state_ = State::SortingChip;
    worker_active_ = true;
    setStatusLocked("开始分拣" + slotText(result));
    worker_ = std::thread([this, result]() {
        const bool ok = servo_ && servo_->sortChip(result.good);
        onSortActionFinished(ok);
    });
}

void SortCycleController::onSortActionFinished(bool ok)
{
    std::lock_guard<std::mutex> lock(mutex_);
    worker_active_ = false;
    if (!ok) {
        state_ = State::Error;
        setStatusLocked("机械臂分拣动作失败");
        return;
    }
    continueAfterSortLocked();
}

void SortCycleController::continueAfterSortLocked()
{
    ++current_sort_index_;
    if (current_sort_index_ < static_cast<int>(results_.size())) {
        requestForwardLocked(config_.chip_spacing_mm,
                             "下一块芯片移动到分拣区",
                             State::WaitingNextChip);
        return;
    }
    requestBackwardLocked(totalReturnDistanceMmLocked(),
                          "分拣完成回初始位置",
                          State::WaitingReturnHome);
}

int SortCycleController::bridgeDistanceMmLocked() const
{
    return config_.first_chip_to_sort_area_mm;
}

int SortCycleController::totalReturnDistanceMmLocked() const
{
    const int detect_forward_mm = (config_.chip_count - 1) * config_.chip_spacing_mm;
    const int sort_forward_mm = (config_.chip_count - 1) * config_.chip_spacing_mm;
    return detect_forward_mm + config_.first_chip_to_sort_area_mm + sort_forward_mm;
}

void SortCycleController::joinWorkerIfFinishedLocked()
{
    if (!worker_active_ && worker_.joinable() &&
        worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}
