#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>

namespace kiebitz::app {

/// Runs `done` with a future's result on the UI thread once the worker has finished.
/// (The watcher belongs to `context` and deletes itself when done; the analyzer can't see Qt's
/// parent ownership and reports a leak, hence the NOLINT.)
template <typename T, typename Done>
void whenReady(QObject* context, const QFuture<T>& future, const Done& done) {
    auto* watcher =
        new QFutureWatcher<T>(context); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    QObject::connect(watcher, &QFutureWatcher<T>::finished, context, [watcher, done] {
        watcher->deleteLater();
        done(watcher->result());
    });
    watcher->setFuture(future);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

} // namespace kiebitz::app
