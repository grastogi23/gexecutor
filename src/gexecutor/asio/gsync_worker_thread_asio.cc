/**
 * gsync_worker_thread.cc
 *  Copyright Admin Cppexecutor, 2013
 *  Created on: Jul 21, 2013
 *      Author: cppexecutor@gmail.com
 */

#include "gexecutor/asio/gsync_worker_thread_asio.h"

GSyncWorkerThreadAsio::GSyncWorkerThreadAsio(GSyncExecutorAsio* sync_executor,
                                             GTaskQSharedPtr taskq,
                                             const std::string& worker_id)
    : sync_executor_(sync_executor), taskq_(taskq), thread_id_(),
      worker_id_(worker_id), async_executor_(NULL)  {
}

GSyncWorkerThreadAsio::~GSyncWorkerThreadAsio() {
    if (async_executor_) {
        delete async_executor_;
        async_executor_ = NULL;
    }
}

/**
 * synchronous worker thread function.
 * @param args
 */
static void *gsync_executor_worker(void *args) {

    GSyncWorkerThreadAsio* p_worker =
            static_cast<GSyncWorkerThreadAsio*>(args);
    GEXECUTOR_LOG(GEXECUTOR_TRACE)
            << "Starting Thread with threadid"
            << p_worker->id() << std::endl;
    p_worker->SetupEventLoop();
    return p_worker;
}

gerror_code_t GSyncWorkerThreadAsio::Initialize() {
    pthread_attr_t attr;
    int rc = 0;
    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        GEXECUTOR_LOG(GEXECUTOR_ERROR)
                << "Error in pthread_attr_init:" << rc << std::endl;
    }
    rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (rc != 0) {
        GEXECUTOR_LOG(GEXECUTOR_ERROR)
                << "Error in pthread_attr_setdetachstate:" << rc << std::endl;
    }
    pthread_attr_setstacksize(&attr, 512*1024);
    /* Create one thread for each command-line argument */
    rc = pthread_create(&thread_id_, &attr,
                        &gsync_executor_worker,
                        this);
    GEXECUTOR_LOG(GEXECUTOR_TRACE)
        << "Created thread with id" << thread_id_ << std::endl;

    rc = pthread_attr_destroy(&attr);
    if (rc != 0) {
        GEXECUTOR_LOG(GEXECUTOR_ERROR)
                << "Error in pthread_attr_destroy:" << rc << std::endl;
    }
    return 0;
}

void GSyncWorkerThreadAsio::SetupEventLoop() {
    GEXECUTOR_LOG(GEXECUTOR_TRACE)
        << "Settting up Event loop for worker\n";
    async_executor_ = new GAsyncExecutorAsio(io_, taskq_);
    async_executor_->Initialize();
}

void GSyncWorkerThreadAsio::Shutdown() {
    async_executor_->Shutdown();
}
