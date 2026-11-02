// beatles - ReXGlue Recompiled Project
//
// Unlinks the guest memory backing file from /dev/shm right after startup.
//
// Why: the SDK runtime backs the guest memory arena with a POSIX shared memory
// object (/dev/shm/xenia_memory_<ticks>, 4.8 GB sparse) and only shm_unlink()s
// it in the Memory destructor. Every abnormal exit (crash, abort, timeout kill,
// and even the normal "Title terminated; hard-exiting process" path) leaked it.
// /dev/shm is RAM-backed, so the leaks piled up (12 files, 2.9 GB), filled the
// tmpfs and made both the game and the Hyprland compositor die with SIGBUS.
//
// How: once the arena is mapped, find our own open descriptors that point at
// /dev/shm/xenia_memory_* and unlink those names. The mappings and the open
// descriptor stay valid; the kernel frees the memory when the process exits,
// however it exits. The runtime's own shm_unlink at shutdown then just fails
// harmlessly with ENOENT.

#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

#include <rex/logging.h>

void UnlinkGuestMemoryShm() {
  namespace fs = std::filesystem;
  constexpr std::string_view kShmDir = "/dev/shm/";
  constexpr std::string_view kPrefix = "xenia_memory_";

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator("/proc/self/fd", ec)) {
    std::error_code link_ec;
    std::string target = fs::read_symlink(entry.path(), link_ec).string();
    if (link_ec || !target.starts_with(kShmDir)) {
      continue;
    }
    std::string name = target.substr(kShmDir.size());
    if (name.ends_with(" (deleted)") || !name.starts_with(kPrefix)) {
      continue;
    }
    if (shm_unlink(("/" + name).c_str()) == 0) {
      REXLOG_INFO("Unlinked guest memory shm object /dev/shm/{} (freed on exit)", name);
    } else {
      REXLOG_WARN("shm_unlink(/{}) failed: errno {}", name, errno);
    }
  }
}
