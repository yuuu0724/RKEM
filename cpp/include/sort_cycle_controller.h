#ifndef SORT_CYCLE_CONTROLLER_H
#define SORT_CYCLE_CONTROLLER_H

#include "chip_sort_result.h"
#include "servo_driver.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class TrayMotionKind {
    Forward,
    Backward
};

struct SortCycleConfig {
    int chip_count = 4;
    int chip_spacing_mm = 20;
    int first_chip_to_sort_area_mm = 100;
};

struct SortCycleCallbacks {
    std::function<bool(int, const std::string&)> move_forward_mm;
    std::function<bool(int, const std::string&)> move_backward_mm;
    std::function<void(const std::string&)> status;
    std::function<void()> completed;
};

class SortCycleController {
public:
    explicit SortCycleController(std::unique_ptr<ServoDriver> servo);
    ~SortCycleController();

    SortCycleController(const SortCycleController&) = delete;
    SortCycleController& operator=(const SortCycleController&) = delete;

    void setCallbacks(const SortCycleCallbacks& callbacks);
    void configure(const SortCycleConfig& config);
    bool init();
    void reset();

    bool recordInspectionResult(const ChipSortResult& result);
    void onTrayMotionDone(TrayMotionKind kind);
    void onTrayMotionInterrupted();

private:
    enum class State {
        Idle,
        Inspecting,
        WaitingFirstToSortArea,
        SortingChip,
        WaitingNextChip,
        WaitingReturnHome,
        Completed,
        Error
    };

    void setStatusLocked(const std::string& message);
    bool requestForwardLocked(int distance_mm, const std::string& label, State wait_state);
    bool requestBackwardLocked(int distance_mm, const std::string& label, State wait_state);
    void startSortingLocked();
    void onSortActionFinished(bool ok);
    void continueAfterSortLocked();
    int bridgeDistanceMmLocked() const;
    int totalReturnDistanceMmLocked() const;
    void joinWorkerIfFinishedLocked();

private:
    SortCycleConfig config_;
    SortCycleCallbacks callbacks_;
    std::unique_ptr<ServoDriver> servo_;
    std::vector<ChipSortResult> results_;
    State state_ = State::Idle;
    int current_sort_index_ = 0;
    std::mutex mutex_;
    std::thread worker_;
    bool worker_active_ = false;
};

#endif // SORT_CYCLE_CONTROLLER_H
