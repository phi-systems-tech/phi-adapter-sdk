#include "phi/adapter/sdk/qt/instance_execution_backend_qt.h"

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QThread>

namespace phicore::adapter::sdk::qt {

namespace {

class QtInstanceExecutionBackend final : public phicore::adapter::sdk::InstanceExecutionBackend
{
public:
    ~QtInstanceExecutionBackend() override
    {
        stop();
    }

    bool start(phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_started)
            return true;

        auto thread = std::make_unique<QThread>();
        auto target = std::make_unique<QObject>();
        target->moveToThread(thread.get());
        thread->start();
        if (!thread->isRunning()) {
            if (error)
                *error = "Qt execution thread failed to start";
            return false;
        }

        m_thread = std::move(thread);
        m_target = std::move(target);
        m_started = true;
        return true;
    }

    bool execute(std::function<void()> task, phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        if (!task) {
            if (error)
                *error = "Execution task is empty";
            return false;
        }

        QObject *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_started || !m_thread || !m_target || !m_thread->isRunning()) {
                if (error)
                    *error = "Qt execution backend not started";
                return false;
            }
            target = m_target.get();
        }

        auto sharedTask = std::make_shared<std::function<void()>>(std::move(task));
        const bool queued = QMetaObject::invokeMethod(
            target,
            [sharedTask]() {
                try {
                    (*sharedTask)();
                } catch (...) {
                    // Keep backend alive if adapter work throws.
                }
            },
            Qt::QueuedConnection);
        if (!queued && error)
            *error = "Failed to enqueue task on Qt execution backend";
        return queued;
    }

    void stop() override
    {
        std::unique_ptr<QThread> thread;
        std::unique_ptr<QObject> target;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_started)
                return;
            m_started = false;
            thread = std::move(m_thread);
            target = std::move(m_target);
        }

        if (target) {
            QObject *rawTarget = target.release();
            if (!QMetaObject::invokeMethod(rawTarget, "deleteLater", Qt::QueuedConnection))
                delete rawTarget;
        }

        if (!thread)
            return;
        thread->quit();
        if (!thread->wait(3000)) {
            thread->terminate();
            thread->wait();
        }
    }

private:
    std::mutex m_mutex;
    std::unique_ptr<QThread> m_thread;
    std::unique_ptr<QObject> m_target;
    bool m_started = false;
};

} // namespace

std::unique_ptr<phicore::adapter::sdk::InstanceExecutionBackend> createInstanceExecutionBackend()
{
    return std::make_unique<QtInstanceExecutionBackend>();
}

} // namespace phicore::adapter::sdk::qt
