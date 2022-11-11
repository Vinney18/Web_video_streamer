#include "Thread.h"


Thread::Thread() : mStop(false), mRunning(false)
{}

Thread::~Thread() {
	//Debug( 1, "Destroying thread %d (%s)", mTid, mThreadLabel.c_str() );
	if (mRunning)
	{
		//Warning( "Destroying still running thread %d (%s)", mTid, mThreadLabel.c_str() );
	}
	join();
}

void Thread::startThread() {

	std::lock_guard<std::mutex> locker(mThreadMutex);
	if (!mRunning) {
		mRunning = true;
		mStop = false;
		mPaused = false;
		mThread = std::thread(&Thread::run, this);
	}
	else {
		// print error
		//Error( "Attempt to start already running thread %d (%s)", mTid, mThreadLabel.c_str() );
	}
}

void Thread::stopThread() {
	//    Debug( 1, "Stopping thread %d (%s)", mTid, mThreadLabel.c_str() );
	mStop = true;
	cv.notify_all();
	join();
}

void Thread::join() {
	//Debug( 1, "Joining thread %d (%s)", mTid, mThreadLabel.c_str() );
	std::lock_guard<std::mutex> locker(mThreadMutex);
	if (mRunning) {
		if (mThread.joinable()) {
			mThread.join();
		}
		mRunning = false;
	}
}
