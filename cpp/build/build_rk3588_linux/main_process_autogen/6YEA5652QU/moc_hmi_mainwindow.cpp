/****************************************************************************
** Meta object code from reading C++ file 'hmi_mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../include/hmi_mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QVector>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'hmi_mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_CameraPreviewWidget_t {
    QByteArrayData data[1];
    char stringdata0[20];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_CameraPreviewWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_CameraPreviewWidget_t qt_meta_stringdata_CameraPreviewWidget = {
    {
QT_MOC_LITERAL(0, 0, 19) // "CameraPreviewWidget"

    },
    "CameraPreviewWidget"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_CameraPreviewWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void CameraPreviewWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

QT_INIT_METAOBJECT const QMetaObject CameraPreviewWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_CameraPreviewWidget.data,
    qt_meta_data_CameraPreviewWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *CameraPreviewWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CameraPreviewWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CameraPreviewWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CameraPreviewWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    return _id;
}
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[62];
    char stringdata0[963];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 15), // "templateChanged"
QT_MOC_LITERAL(2, 27, 0), // ""
QT_MOC_LITERAL(3, 28, 12), // "chipTemplate"
QT_MOC_LITERAL(4, 41, 16), // "matchModeChanged"
QT_MOC_LITERAL(5, 58, 9), // "matchMode"
QT_MOC_LITERAL(6, 68, 19), // "ocrThresholdChanged"
QT_MOC_LITERAL(7, 88, 9), // "threshold"
QT_MOC_LITERAL(8, 98, 20), // "defectOptionsChanged"
QT_MOC_LITERAL(9, 119, 17), // "pinMissingEnabled"
QT_MOC_LITERAL(10, 137, 14), // "scratchEnabled"
QT_MOC_LITERAL(11, 152, 24), // "startInspectionRequested"
QT_MOC_LITERAL(12, 177, 12), // "ocrThreshold"
QT_MOC_LITERAL(13, 190, 23), // "employeeDialogRequested"
QT_MOC_LITERAL(14, 214, 23), // "employeeSignInRequested"
QT_MOC_LITERAL(15, 238, 24), // "employeeSignOutRequested"
QT_MOC_LITERAL(16, 263, 22), // "employeeLeaveRequested"
QT_MOC_LITERAL(17, 286, 23), // "employeeReturnRequested"
QT_MOC_LITERAL(18, 310, 18), // "updateDeviceStatus"
QT_MOC_LITERAL(19, 329, 12), // "DeviceStatus"
QT_MOC_LITERAL(20, 342, 6), // "status"
QT_MOC_LITERAL(21, 349, 18), // "updateSystemStatus"
QT_MOC_LITERAL(22, 368, 12), // "SystemStatus"
QT_MOC_LITERAL(23, 381, 13), // "updateKpiData"
QT_MOC_LITERAL(24, 395, 7), // "KpiData"
QT_MOC_LITERAL(25, 403, 4), // "data"
QT_MOC_LITERAL(26, 408, 9), // "updateKpi"
QT_MOC_LITERAL(27, 418, 10), // "totalCount"
QT_MOC_LITERAL(28, 429, 9), // "goodCount"
QT_MOC_LITERAL(29, 439, 8), // "badCount"
QT_MOC_LITERAL(30, 448, 8), // "goodRate"
QT_MOC_LITERAL(31, 457, 16), // "updateSlotResult"
QT_MOC_LITERAL(32, 474, 10), // "SlotResult"
QT_MOC_LITERAL(33, 485, 6), // "result"
QT_MOC_LITERAL(34, 492, 9), // "slotIndex"
QT_MOC_LITERAL(35, 502, 9), // "chipModel"
QT_MOC_LITERAL(36, 512, 10), // "textResult"
QT_MOC_LITERAL(37, 523, 12), // "defectResult"
QT_MOC_LITERAL(38, 536, 6), // "reason"
QT_MOC_LITERAL(39, 543, 20), // "updateAllSlotResults"
QT_MOC_LITERAL(40, 564, 19), // "QVector<SlotResult>"
QT_MOC_LITERAL(41, 584, 7), // "results"
QT_MOC_LITERAL(42, 592, 20), // "updateEmployeeStatus"
QT_MOC_LITERAL(43, 613, 14), // "EmployeeStatus"
QT_MOC_LITERAL(44, 628, 18), // "updateLeaveRecords"
QT_MOC_LITERAL(45, 647, 20), // "QVector<QStringList>"
QT_MOC_LITERAL(46, 668, 7), // "records"
QT_MOC_LITERAL(47, 676, 20), // "updateFatigueRecords"
QT_MOC_LITERAL(48, 697, 17), // "updateCameraFrame"
QT_MOC_LITERAL(49, 715, 8), // "cameraId"
QT_MOC_LITERAL(50, 724, 5), // "frame"
QT_MOC_LITERAL(51, 730, 12), // "refreshClock"
QT_MOC_LITERAL(52, 743, 18), // "showEmployeeDialog"
QT_MOC_LITERAL(53, 762, 25), // "confirmInspectionTemplate"
QT_MOC_LITERAL(54, 788, 19), // "requestMotorForward"
QT_MOC_LITERAL(55, 808, 16), // "requestMotorBack"
QT_MOC_LITERAL(56, 825, 19), // "requestMotorAutoRun"
QT_MOC_LITERAL(57, 845, 21), // "requestEmployeeEnroll"
QT_MOC_LITERAL(58, 867, 22), // "requestEmployeeCheckin"
QT_MOC_LITERAL(59, 890, 23), // "requestEmployeeCheckout"
QT_MOC_LITERAL(60, 914, 21), // "requestEmployeeDelete"
QT_MOC_LITERAL(61, 936, 26) // "onEmployeeOperationTimeout"

    },
    "MainWindow\0templateChanged\0\0chipTemplate\0"
    "matchModeChanged\0matchMode\0"
    "ocrThresholdChanged\0threshold\0"
    "defectOptionsChanged\0pinMissingEnabled\0"
    "scratchEnabled\0startInspectionRequested\0"
    "ocrThreshold\0employeeDialogRequested\0"
    "employeeSignInRequested\0"
    "employeeSignOutRequested\0"
    "employeeLeaveRequested\0employeeReturnRequested\0"
    "updateDeviceStatus\0DeviceStatus\0status\0"
    "updateSystemStatus\0SystemStatus\0"
    "updateKpiData\0KpiData\0data\0updateKpi\0"
    "totalCount\0goodCount\0badCount\0goodRate\0"
    "updateSlotResult\0SlotResult\0result\0"
    "slotIndex\0chipModel\0textResult\0"
    "defectResult\0reason\0updateAllSlotResults\0"
    "QVector<SlotResult>\0results\0"
    "updateEmployeeStatus\0EmployeeStatus\0"
    "updateLeaveRecords\0QVector<QStringList>\0"
    "records\0updateFatigueRecords\0"
    "updateCameraFrame\0cameraId\0frame\0"
    "refreshClock\0showEmployeeDialog\0"
    "confirmInspectionTemplate\0requestMotorForward\0"
    "requestMotorBack\0requestMotorAutoRun\0"
    "requestEmployeeEnroll\0requestEmployeeCheckin\0"
    "requestEmployeeCheckout\0requestEmployeeDelete\0"
    "onEmployeeOperationTimeout"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      32,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      10,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,  174,    2, 0x06 /* Public */,
       4,    1,  177,    2, 0x06 /* Public */,
       6,    1,  180,    2, 0x06 /* Public */,
       8,    2,  183,    2, 0x06 /* Public */,
      11,    5,  188,    2, 0x06 /* Public */,
      13,    0,  199,    2, 0x06 /* Public */,
      14,    0,  200,    2, 0x06 /* Public */,
      15,    0,  201,    2, 0x06 /* Public */,
      16,    0,  202,    2, 0x06 /* Public */,
      17,    0,  203,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      18,    1,  204,    2, 0x0a /* Public */,
      21,    1,  207,    2, 0x0a /* Public */,
      23,    1,  210,    2, 0x0a /* Public */,
      26,    4,  213,    2, 0x0a /* Public */,
      31,    1,  222,    2, 0x0a /* Public */,
      31,    6,  225,    2, 0x0a /* Public */,
      39,    1,  238,    2, 0x0a /* Public */,
      42,    1,  241,    2, 0x0a /* Public */,
      44,    1,  244,    2, 0x0a /* Public */,
      47,    1,  247,    2, 0x0a /* Public */,
      48,    2,  250,    2, 0x0a /* Public */,
      51,    0,  255,    2, 0x08 /* Private */,
      52,    0,  256,    2, 0x08 /* Private */,
      53,    0,  257,    2, 0x08 /* Private */,
      54,    0,  258,    2, 0x08 /* Private */,
      55,    0,  259,    2, 0x08 /* Private */,
      56,    0,  260,    2, 0x08 /* Private */,
      57,    0,  261,    2, 0x08 /* Private */,
      58,    0,  262,    2, 0x08 /* Private */,
      59,    0,  263,    2, 0x08 /* Private */,
      60,    0,  264,    2, 0x08 /* Private */,
      61,    0,  265,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void, QMetaType::Bool, QMetaType::Bool,    9,   10,
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::Double, QMetaType::Bool, QMetaType::Bool,    3,    5,   12,    9,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 19,   20,
    QMetaType::Void, 0x80000000 | 22,   20,
    QMetaType::Void, 0x80000000 | 24,   25,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int, QMetaType::Double,   27,   28,   29,   30,
    QMetaType::Void, 0x80000000 | 32,   33,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::QString, QMetaType::QString,   34,   20,   35,   36,   37,   38,
    QMetaType::Void, 0x80000000 | 40,   41,
    QMetaType::Void, 0x80000000 | 43,   20,
    QMetaType::Void, 0x80000000 | 45,   46,
    QMetaType::Void, 0x80000000 | 45,   46,
    QMetaType::Void, QMetaType::Int, QMetaType::QImage,   49,   50,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->templateChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->matchModeChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->ocrThresholdChanged((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 3: _t->defectOptionsChanged((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 4: _t->startInspectionRequested((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5]))); break;
        case 5: _t->employeeDialogRequested(); break;
        case 6: _t->employeeSignInRequested(); break;
        case 7: _t->employeeSignOutRequested(); break;
        case 8: _t->employeeLeaveRequested(); break;
        case 9: _t->employeeReturnRequested(); break;
        case 10: _t->updateDeviceStatus((*reinterpret_cast< const DeviceStatus(*)>(_a[1]))); break;
        case 11: _t->updateSystemStatus((*reinterpret_cast< const SystemStatus(*)>(_a[1]))); break;
        case 12: _t->updateKpiData((*reinterpret_cast< const KpiData(*)>(_a[1]))); break;
        case 13: _t->updateKpi((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< double(*)>(_a[4]))); break;
        case 14: _t->updateSlotResult((*reinterpret_cast< const SlotResult(*)>(_a[1]))); break;
        case 15: _t->updateSlotResult((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5])),(*reinterpret_cast< const QString(*)>(_a[6]))); break;
        case 16: _t->updateAllSlotResults((*reinterpret_cast< const QVector<SlotResult>(*)>(_a[1]))); break;
        case 17: _t->updateEmployeeStatus((*reinterpret_cast< const EmployeeStatus(*)>(_a[1]))); break;
        case 18: _t->updateLeaveRecords((*reinterpret_cast< const QVector<QStringList>(*)>(_a[1]))); break;
        case 19: _t->updateFatigueRecords((*reinterpret_cast< const QVector<QStringList>(*)>(_a[1]))); break;
        case 20: _t->updateCameraFrame((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QImage(*)>(_a[2]))); break;
        case 21: _t->refreshClock(); break;
        case 22: _t->showEmployeeDialog(); break;
        case 23: _t->confirmInspectionTemplate(); break;
        case 24: _t->requestMotorForward(); break;
        case 25: _t->requestMotorBack(); break;
        case 26: _t->requestMotorAutoRun(); break;
        case 27: _t->requestEmployeeEnroll(); break;
        case 28: _t->requestEmployeeCheckin(); break;
        case 29: _t->requestEmployeeCheckout(); break;
        case 30: _t->requestEmployeeDelete(); break;
        case 31: _t->onEmployeeOperationTimeout(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 18:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QVector<QStringList> >(); break;
            }
            break;
        case 19:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QVector<QStringList> >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MainWindow::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::templateChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::matchModeChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::ocrThresholdChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(bool , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::defectOptionsChanged)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(const QString & , const QString & , double , bool , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::startInspectionRequested)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::employeeDialogRequested)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::employeeSignInRequested)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::employeeSignOutRequested)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::employeeLeaveRequested)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::employeeReturnRequested)) {
                *result = 9;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 32)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 32)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::templateChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void MainWindow::matchModeChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void MainWindow::ocrThresholdChanged(double _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void MainWindow::defectOptionsChanged(bool _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void MainWindow::startInspectionRequested(const QString & _t1, const QString & _t2, double _t3, bool _t4, bool _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void MainWindow::employeeDialogRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void MainWindow::employeeSignInRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void MainWindow::employeeSignOutRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void MainWindow::employeeLeaveRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void MainWindow::employeeReturnRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
