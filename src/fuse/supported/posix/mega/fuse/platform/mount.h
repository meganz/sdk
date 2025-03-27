#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <mega/fuse/common/activity_monitor.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/inode_forward.h>
#include <mega/fuse/common/mount.h>
#include <mega/fuse/common/mount_inode_id_forward.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/tags.h>
#include <mega/fuse/common/task_executor.h>
#include <mega/fuse/common/task_executor_flags_forward.h>
#include <mega/fuse/platform/inode_invalidator.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_forward.h>
#include <mega/fuse/platform/request_forward.h>
#include <mega/fuse/platform/session.h>

namespace mega
{
namespace fuse
{
namespace platform
{

class Mount final
  : public fuse::Mount
{
    friend class Session;
    friend class SessionBase;

    void access(Request request,
                MountInodeID inode,
                int mask);

    void destroy();

    void doUnlink(Request request,
                  MountInodeID parent,
                  std::function<Error(InodeRef)> predicate,
                  const std::string& name);

    template<typename Callback, typename... Arguments>
    static constexpr auto IsMountCallbackV =
      std::is_invocable_r_v<void, Callback, Mount*, Arguments...>;

    template<typename... Arguments,
             typename Callback>
    auto execute(Callback callback,
                 bool spawnWorker,
                 Arguments&&... arguments)
      -> std::enable_if_t<IsMountCallbackV<Callback, Arguments...>>
    {
        std::function<void()> callback_ =
          std::bind(callback,
                    this,
                    std::forward<Arguments>(arguments)...);

        auto wrapper = [](Activity, auto& callback, const Task&) {
            callback();
        }; // wrapper

        std::function<void(const Task&)> wrapper_ =
            std::bind(std::move(wrapper),
                      mActivities.begin(),
                      std::move(callback_),
                      std::placeholders::_1);

        mExecutor.execute(std::move(wrapper_), spawnWorker);
    }

    void lookup(Request request,
                MountInodeID parent,
                const std::string& name);

    void flush(Request request,
               MountInodeID inode,
               fuse_file_info& info);

    void forget(Request request,
                MountInodeID inode,
                std::size_t num);

    void forget_multi(Request request,
                      const std::vector<fuse_forget_data>& forgets);

    void fsync(Request request,
               MountInodeID inode,
               bool onlyData,
               fuse_file_info& info);

    void getattr(Request request,
                 MountInodeID inode);

    void mkdir(Request request,
               MountInodeID parent,
               const std::string& name,
               mode_t mode);

    void mknod(Request request,
               MountInodeID parent,
               const std::string& name,
               mode_t mode);

    void open(Request request,
              MountInodeID inode,
              fuse_file_info& info);

    void opendir(Request request,
                 MountInodeID inode,
                 fuse_file_info& info);

    void read(Request request,
              MountInodeID inode,
              std::size_t size,
              off_t offset,
              fuse_file_info& info);

    void readdir(Request request,
                 MountInodeID inode,
                 std::size_t size,
                 off_t offset,
                 fuse_file_info& info);

    void release(Request request,
                 MountInodeID inode,
                 fuse_file_info& info);

    void releasedir(Request request,
                    MountInodeID inode,
                    fuse_file_info& info);

    void rename(Request request,
                MountInodeID sourceParent,
                const std::string& sourceName,
                MountInodeID targetParent,
                const std::string& targetName,
                unsigned int flags);

    void rmdir(Request request,
               MountInodeID parent,
               const std::string& name);

    void setattr(Request request,
                 MountInodeID inode,
                 struct stat& attributes,
                 int changes);

    void statfs(Request request,
                MountInodeID inode);

    void unlink(Request request,
                MountInodeID parent,
                const std::string& name);

    void write(Request request,
               MountInodeID inode,
               const std::string& data,
               off_t offset,
               fuse_file_info& info);

    // Tracks whether any requests are in progress.
    ActivityMonitor mActivities;

    // Responsible for performing requests.
    TaskExecutor mExecutor;

    // Where is the mount mounted?
    NormalizedPath mPath;

    // How this mount communicates with libfuse.
    Session mSession;

    // Responsible for invalidating inodes.
    InodeInvalidator mInvalidator;

public:
    Mount(const MountInfo& info,
          MountDB& mountDB);

    ~Mount();

    // Update this mount's executor flags.
    void executorFlags(const TaskExecutorFlags& flags) override;

    // Invalidate an inode's attributes.
    void invalidateAttributes(InodeID id) override;

    // Invalidate an inode's data.
    void invalidateData(InodeID id,
                        m_off_t offset,
                        m_off_t size) override;

    void invalidateData(InodeID id) override;

    // Invalidate a directory entry.
    void invalidateEntry(const std::string& name,
                         InodeID child,
                         InodeID parent) override;

    void invalidateEntry(const std::string& name,
                         InodeID parent) override;

    // Translate a mount-speicifc inode ID to a system-wide inode ID.
    InodeID map(MountInodeID id) const override;

    // Translate a system-wide inode ID to a mount-specific inode ID.
    MountInodeID map(InodeID id) const override;

    // What local path is this mount mapping from?
    NormalizedPath path() const override;
}; // Mount

} // platform
} // fuse
} // mega

