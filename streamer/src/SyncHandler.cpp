#include "SyncHandler.h"
#include "AppConfig.h"

SyncHandler::SyncHandler()
	: logger_(AppConfig::instance().logger())
{
}

SyncHandler::~SyncHandler()
{
	// Release all pending groups so blocked FFmpeg threads can exit
	std::lock_guard<std::mutex> gLock(groupsMutex_);
	for (auto& [id, info] : groups_)
	{
		std::lock_guard<std::mutex> lock(info->mutex);
		info->released = true;
		info->cv.notify_all();
	}
	groups_.clear();
}

void SyncHandler::incrementExpectedCount(const std::string& groupId, const std::string& clientId)
{
	std::lock_guard<std::mutex> gLock(groupsMutex_);
	auto it = groups_.find(groupId);
	if (it == groups_.end())
	{
		auto info = std::make_shared<SyncGroupInfo>();
		info->clientIds.insert(clientId);
		info->createdAt = std::chrono::steady_clock::now();
		groups_[groupId] = info;

		if (logger_)
		{
			logger_->info("SyncHandler: created group '{}', client '{}', expected count = 1", groupId, clientId);
		}
	}
	else
	{
		it->second->clientIds.insert(clientId);

		if (logger_)
		{
			logger_->info("SyncHandler: group '{}' client '{}' added, expected count = {}",
						  groupId, clientId, it->second->clientIds.size());
		}
	}
}

void SyncHandler::waitForReady(const std::string& groupId, FFmpegWrapper* ffmpeg)
{
	std::shared_ptr<SyncGroupInfo> info;
	{
		std::lock_guard<std::mutex> gLock(groupsMutex_);
		auto it = groups_.find(groupId);
		if (it == groups_.end())
		{
			return;  // No group registered — play immediately
		}
		info = it->second;
	}

	bool shouldRelease = false;
	{
		std::lock_guard<std::mutex> lock(info->mutex);
		if (info->released)
		{
			return;  // Already released — play immediately
		}

		info->ffmpegRefs.insert(ffmpeg);

		if (logger_)
		{
			logger_->info("SyncHandler: group '{}' — {}/{} FFmpeg instances ready",
						  groupId, info->ffmpegRefs.size(), info->clientIds.size());
		}

		if (info->ffmpegRefs.size() >= info->clientIds.size())
		{
			shouldRelease = true;
		}
	}

	if (shouldRelease)
	{
		// Last member — release everyone
		{
			std::lock_guard<std::mutex> lock(info->mutex);
			info->released = true;
		}
		info->cv.notify_all();

		if (logger_)
		{
			logger_->info("SyncHandler: group '{}' — all {} members ready, starting playback",
						  groupId, info->clientIds.size());
		}
		return;
	}

	// Not all ready yet — wait on condition variable
	{
		std::unique_lock<std::mutex> lock(info->mutex);
		bool completed = info->cv.wait_for(
			lock,
			std::chrono::seconds(TIMEOUT_SECONDS),
			[&info]() { return info->released; });

		if (!completed)
		{
			if (logger_)
			{
				logger_->warn("SyncHandler: group '{}' — timeout after {}s, starting with {}/{} members",
							  groupId, TIMEOUT_SECONDS,
							  info->ffmpegRefs.size(), info->clientIds.size());
			}

			// First thread to timeout becomes the releaser
			if (!info->released)
			{
				info->released = true;
				lock.unlock();
				info->cv.notify_all();
			}
		}
	}
}

void SyncHandler::removeClient(const std::string& groupId, const std::string& clientId)
{
	removeMember(groupId, clientId, nullptr);
}

void SyncHandler::removeMember(const std::string& groupId, const std::string& clientId, FFmpegWrapper* ffmpeg)
{
	std::shared_ptr<SyncGroupInfo> info;
	{
		std::lock_guard<std::mutex> gLock(groupsMutex_);
		auto it = groups_.find(groupId);
		if (it == groups_.end())
		{
			return;
		}
		info = it->second;
	}

	bool shouldRelease = false;
	bool groupEmpty = false;
	{
		std::lock_guard<std::mutex> lock(info->mutex);
		info->ffmpegRefs.erase(ffmpeg);
		info->clientIds.erase(clientId);

		if (logger_)
		{
			logger_->info("SyncHandler: group '{}' — client '{}' removed, {}/{} remaining",
						  groupId, clientId, info->ffmpegRefs.size(), info->clientIds.size());
		}

		groupEmpty = info->clientIds.empty();

		// If group is still waiting and remaining members now satisfy the count, release
		if (!info->released && !groupEmpty &&
			info->ffmpegRefs.size() >= info->clientIds.size())
		{
			shouldRelease = true;
		}
	}

	if (groupEmpty)
	{
		// No members left — remove group entirely
		{
			std::lock_guard<std::mutex> lock(info->mutex);
			info->released = true;
		}
		info->cv.notify_all();

		std::lock_guard<std::mutex> gLock(groupsMutex_);
		groups_.erase(groupId);
		return;
	}

	if (shouldRelease)
	{
		{
			std::lock_guard<std::mutex> lock(info->mutex);
			info->released = true;
		}
		info->cv.notify_all();
	}
}

void SyncHandler::removeGroup(const std::string& groupId)
{
	std::shared_ptr<SyncGroupInfo> info;
	{
		std::lock_guard<std::mutex> gLock(groupsMutex_);
		auto it = groups_.find(groupId);
		if (it == groups_.end())
		{
			return;
		}
		info = it->second;
		groups_.erase(it);
	}

	// Release any waiting threads
	{
		std::lock_guard<std::mutex> lock(info->mutex);
		info->released = true;
	}
	info->cv.notify_all();
}
