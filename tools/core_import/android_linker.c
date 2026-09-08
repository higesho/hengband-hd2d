// ld.lld の動作モードを APK 内のファイル名に依存させない。
#include <unistd.h>
#include <string.h>
#include <sys/prctl.h>
#include <signal.h>
int main(int argc, char **argv) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) return 127;
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path)-1);
    if (n < 0) return 127;
    path[n] = 0;
    char *slash = strrchr(path, '/');
    if (!slash || (size_t)(slash-path)+12 >= sizeof(path)) return 127;
    strcpy(slash+1, "libhblld.so");
    argv[0] = "ld.lld";
    execv(path, argv);
    return 127;
}
