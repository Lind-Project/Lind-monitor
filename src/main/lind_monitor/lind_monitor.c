/*
 * lind_monitor.c
 *
 *  Created on: April 17, 2014
 *      Author: Ali Gholami, Shengqian Ji
 */

#include "lind_monitor.h"
#include <string.h>

#include "../platform/lind_platform.h"


int main(int argc, char** argv)
{

	/* check the command line arguments to see if a process is defined for trace */
	if (argc <= 0) {
		fprintf(stderr, "Usage %s <program> <options>\n", argv[0]);
		exit(-1);
	}

	init_ptrace(argc, argv);
	LindPythonInit();
	intercept_calls();
	return 0;
}

/* initialize a process to be traced */
void init_ptrace(int argc, char** argv)
{

	char ** argv1 = malloc (sizeof (char* )* argc);
	memcpy(argv1, argv+1,sizeof (char* )* (argc-1));
	argv1[argc-1]=NULL;

	load_config();
	tracee = fork();

	/* check if fork was successful */
	if (tracee < 0) {
		fprintf(stderr, "No process could be monitored. \n");
		exit(-1);
	}

	/* trace the child */
	if (tracee == 0) {
		/* to let the parent process to trace the child*/
		ptrace(PTRACE_TRACEME, tracee, 0, 0);

		/* stop the current process*/
		kill(getpid(), SIGSTOP);

		extern char **environ;


		execve(argv[1], argv1, environ);


		fprintf(stderr, "Unknown command %s\n", argv[1]);
		exit(1);
	}

}

/* intercept the system calls issued by the tracee process */
void intercept_calls()
{
	int entering = 1;
	int status = -1, syscall_num = -1;
	char *path;
	char *path1;
	char *var;
	void *buff;
	struct syscall_args regs, regs_orig;
	char *execve_path;
	char **execve_args;
	/* wait for the child to stop */
	waitpid(tracee, &status, 0);

	ptrace(PTRACE_SETOPTIONS, tracee, 0,
			PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEEXIT);

	while (1) {

		/* to get every sysacall and be notified in the tracee stops */
		ptrace(PTRACE_SYSCALL, tracee, 0, 0);

		/* wait for other syscalls */
		waitpid(tracee, &status, 0);

		/* if tracee is terminated */
		if (WIFEXITED(status))
			break;

		if (!WIFSTOPPED(status)) {
			fprintf(stderr, "wait(&status)=%d\n", status);
			exit(0);
		}

		get_args(&regs);

		if ((WSTOPSIG(status) == SIGTRAP)
				&& (status & (PTRACE_EVENT_EXIT << 8))) {

		if (regs.syscall != __NR_execve)
				fprintf(stdout, "[monitor] %s(%d)\n", syscall_names[regs.syscall], regs.arg1);

		} else if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {

			if (entering == 1) {

				syscall_num = regs.syscall;
				regs_orig = regs;

				if (monitor_actions[syscall_num] == DENY_LIND) {
					fprintf(stdout, "[monitor] Deny call by Lind: %s %d\n",
							syscall_names[syscall_num], regs.retval);
					regs.retval = EINVAL;
					set_args(&regs);
					break;
				}

				entering = 0;

				switch (syscall_num) {

				case __NR_close:
					if ((int32_t) regs.arg1 >= 0) {
						regs.arg1 = get_mapping(regs.arg1);
						set_args(&regs);
					}
					break;

				case __NR_mmap:
					if (((int32_t) regs.arg5) >= 0) {
						regs.arg5 = get_mapping(regs.arg5);
						set_args(&regs);
					}
					break;

				case __NR_execve:
					 execve_path = get_path(regs.arg1);
					 char** p;
					 int i=0, argc;
					 while(1) {
						 p=(char**)get_mem(regs.arg2+i*sizeof(char*), sizeof (char*));
						 ++i;
						 if(!*p)
							 break;
					 }
					 argc = i;
					 execve_args = malloc(sizeof(char*)*argc);
					 execve_args[argc-1] = 0;
					 i=0;
				     fprintf(stdout, "[monitor] execve(%s, [", execve_path);
					 for(i=0; i<argc; ++i) {
						 p=get_mem(regs.arg2+i*sizeof(char*), sizeof (char*));
						 if(*p) {
							 execve_args[i] = get_path(*p);
							 fprintf(stdout, "[monitor] %s ", execve_args[i]);
						 } else
							 printf("]");
					 }
					 fprintf(stdout, "[monitor] ) = %d \n", regs.retval);
					break;

				default:
					break;
				}
			} else {
				/* get the tracee registers */
				entering = 1;


				if (monitor_actions[syscall_num] == ALLOW_OS) {

					switch (syscall_num) {

					/* if pwritev is allowed by OS */
					case __NR_pwritev:
						//regs.arg1 = get_mapping(regs.arg1);
						//set_args(&regs);
						break;

					case __NR_mmap:
							fprintf(stdout, "[monitor] mmap()=0x%jx \n", regs.retval);
						break;

					case __NR_brk:
						fprintf(stdout, "[monitor] brk()=0x%jx \n", regs.retval);
						break;

					default:
						fprintf(stdout, "[monitor] %s()=%d \n",
								syscall_names[syscall_num], regs.retval);
						break;
					} /* switch*/

				} else if (monitor_actions[syscall_num] == ALLOW_LIND) {

					int lind_fd;
					struct lind_stat st;
					struct lind_statfs stfs;

					switch (syscall_num) {

					case __NR_getuid:
						regs.retval = lind_getuid();
						fprintf(stdout, "[monitor] getuid() = %d \n", (int) regs.retval);
						break;

					case __NR_read:
						buff = malloc(regs.arg3);
						regs.retval = lind_read(regs.arg1, buff, regs.arg3);
						set_mem(regs.arg2, buff, regs.arg3);
						fprintf(stdout,
								"read(%ld, 0x%lx[\"%p\"], %ld) = %d \n",
								regs.arg1, regs.arg2, buff, regs.arg3,
								regs.retval);
						break;

					case __NR_open:
							path = get_path(regs.arg1);
							lind_fd = lind_open(path, regs.arg2, regs.arg3);

							if (lind_fd  >= 0){
								add_mapping(regs.retval, lind_fd);
								regs.retval = lind_fd;
							}else {
								regs.retval = -1;
							}
							fprintf(stdout, "[monitor] open(%s, %d, %d) = %d\n", path, (int) regs.arg2, (int) regs.arg3, (int) regs.retval);

							break;

					case __NR_openat:
							path = get_path(regs.arg2);
							lind_fd = lind_openat(regs.arg1, path, regs.arg3, regs.arg4);

							if (lind_fd  >= 0){
								add_mapping(regs.retval, lind_fd);
								regs.retval = lind_fd;
							}else {
								regs.retval = -1;
							}
							fprintf(stdout, "[monitor] openat(%d, %s, %d, %d) = %d\n", (int) regs.arg1, path, (int) regs.arg2, (int) regs.arg4, (int) regs.retval);
							break;

					case __NR_access:
						path = get_path(regs.arg1);

						lind_fd = lind_access(path, regs.arg2);
						if (lind_fd  >= 0){
							add_mapping(regs.retval, lind_fd);
							regs.retval = lind_fd;
							}else {
								regs.retval = -1;
							}
						fprintf(stdout, "[monitor] access(%s, %d, %d) = %d\n", path, (int) regs.arg2, (int) regs.arg3,
											(int) regs.retval);

						break;

					case __NR_close:
						//lind_fd = get_mapping(regs.arg1);
						regs.retval = lind_close(regs_orig.arg1);
						fprintf(stdout, "[monitor] close(%d) = %d \n", (int) regs_orig.arg1, (int) regs.retval);
						break;

					case __NR_rmdir:
						path = get_path(regs.arg1);
						regs.retval = lind_rmdir(path);
						fprintf(stdout, "[monitor] rmdir(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_statfs:
						path = get_path(regs.arg1);
						regs.retval = lind_statfs(path, &stfs);
						set_mem(regs.arg2, &stfs, sizeof(stfs));
						fprintf(stdout, "[monitor] statfs(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_stat:
						path = get_path(regs.arg1);
						regs.retval = lind_stat(path, &st);
						set_mem(regs.arg2, &st, sizeof(st));
						fprintf(stdout, "[monitor] stat(%s) = %d \n", path, (int) regs.retval);

						break;

					case __NR_fstat:
						regs.retval = lind_fstat(regs.arg1, &st);
						set_mem(regs.arg2, &st, sizeof(st));
						fprintf(stdout, "[monitor] fstat(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_fstatfs:
						regs.retval = lind_fstatfs(regs.arg1, &stfs);
						set_mem(regs.arg2, &stfs, sizeof(stfs));
						fprintf(stdout, "[monitor] fstatfs(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_write:
						regs.retval = lind_write(regs.arg1,
								get_mem(regs.arg2, regs.arg3), regs.arg3);
						fprintf(stdout,
								"write(%ld, 0x%lx[\"%p\"], %ld) = %d \n",
								(int) regs.arg1, (int) regs.arg2,
								get_mem(regs.arg2, regs.arg3), (int) regs.arg3,
								(int) regs.retval);
						break;

					case __NR_mkdir:
						path = get_path(regs.arg1);
						regs.retval = lind_mkdir(path, regs.arg2);
						fprintf(stdout, "[monitor] mkdir(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_chdir:
						path = get_path(regs.arg1);
						regs.retval = lind_chdir(path);
						fprintf(stdout, "[monitor] chdir(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_getcwd:
						path = get_mem(regs.arg1, regs.arg2);
						regs.retval = lind_getcwd(path, regs.arg2);
						fprintf(stdout, "[monitor] getcwd(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_dup:
						regs.retval = lind_dup(regs.arg1);
						fprintf(stdout, "[monitor] dup(%ld) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_dup2:
						regs.retval = lind_dup2(regs.arg1, regs.arg2);
						fprintf(stdout, "[monitor] dup2(%d, %d) = %d \n", (int) regs.arg1, (int) regs.arg2,
								(int) regs.retval);
						break;

					case __NR_dup3:
						regs.retval = lind_dup3(regs.arg1, regs.arg2, regs.arg3);
						fprintf(stdout, "[monitor] dup3(%d, %d, %d) = %d \n", (int) regs.arg1, (int) regs.arg2, (int) regs.arg3,
								(int) regs.retval);
						break;

					case __NR_getpid:
						regs.retval = lind_getpid();
						fprintf(stdout, "[monitor] getpid() = %d \n", (int) regs.retval);
						break;

					case __NR_geteuid:
						regs.retval = lind_geteuid();
						fprintf(stdout, "[monitor] geteuid() = %d \n", (int) regs.retval);
						break;

					case __NR_getgid:
						regs.retval = lind_getgid();
						fprintf(stdout, "[monitor] getgid() = %d \n", (int) regs.retval);
						break;

					case __NR_getegid:
						regs.retval = lind_getegid();
						fprintf(stdout, "[monitor] getegid() = %d \n", (int) regs.retval);
						break;

					case __NR_unlink:
						path = get_path(regs.arg1);
						regs.retval = lind_unlink(path);
						fprintf(stdout, "[monitor] unlink(%s) = %d \n", path, (int) regs.retval);
						break;

					case __NR_link:
						path = get_path(regs.arg1);
						path1 = get_path(regs.arg2);
						regs.retval = lind_link(path, path1);
						fprintf(stdout, "[monitor] link(%s, %s) = %d \n", path, path1,
								(int) regs.retval);
						break;

					case __NR_fcntl:
						regs.retval = lind_fcntl(regs.arg1, regs.arg2);
						fprintf(stdout, "[monitor] fcntl(%ld, %ld) = %d \n", (int) regs.arg1,
								(int) regs.arg2, (int) regs.retval);
						break;

					case __NR_listen:
						regs.retval = lind_listen(regs.arg1, regs.arg2);
						fprintf(stdout, "[monitor] listen(%d, %d) = %d \n", (int) regs.arg1,
								(int) regs.arg2, (int) regs.retval);
						break;

					case __NR_shutdown:
						regs.retval = lind_shutdown(regs.arg1, regs.arg2);
						fprintf(stdout, "[monitor] shutdown(%d, %d) = %d \n", (int) regs.arg1,
								(int) regs.arg2, (int) regs.retval);
						break;

					case __NR_flock:
						regs.retval = lind_flock(regs.arg1, regs.arg2);
						fprintf(stdout, "[monitor] flock(%d, %d) = %d \n", (int) regs.arg1,
								(int) regs.arg2, (int) regs.retval);
						break;

					case __NR_epoll_create:
						regs.retval = lind_epoll_create(regs.arg1);
						fprintf(stdout, "[monitor] epoll_create(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_getdents:
						regs.retval = lind_getdents(regs.arg1,
								get_mem(regs.arg2, regs.arg3), regs.arg3);

						fprintf(stdout, "[monitor] getdents(%d, %d) = %d \n", regs.arg1, regs.arg3, regs.retval);
						break;

					case __NR_lseek:
						regs.retval = lind_lseek(regs.arg1, regs.arg2,
								regs.arg3);
						fprintf(stdout, "[monitor] lseek(%u, %lld, %u) = %d \n",
								regs.arg1, regs.arg2, regs.arg3, regs.retval);
						break;

					case __NR_pread64:
						regs.retval = lind_pread(regs.arg1,
								get_mem(regs.arg2, regs.arg3), regs.arg3,
								regs.arg4);
						fprintf(stdout,
								"pread64(%u, 0x%lx[\"%p\"], %z, %lld) = %d \n",
								regs.arg1, regs.arg2, var, regs.arg3, regs.arg4,
								regs.retval);
						break;

					case __NR_pwritev:
						//regs.arg1 = get_mapping(regs.arg1);
						regs.retval = lind_pwrite(regs.arg1,
								get_mem(regs.arg2, regs.arg3), regs.arg3,
								regs.arg4);
						fprintf(stdout,
								"pwritev(%u, 0x%lx[\"%p\"], %z, %lld) = %d \n",
								regs.arg1, regs.arg2,
								get_mem(regs.arg2, regs.arg3), regs.arg3,
								regs.arg4, regs.retval);
						break;

					case __NR_socket:
						if (regs.arg1 == AF_INET) {
							regs.retval = lind_socket(regs.arg1, regs.arg2,
									regs.arg3);
							fprintf(stdout, "[monitor] socket(%d, %d, %d) = %d \n",
									(int) regs.arg1, (int) regs.arg2, (int) regs.arg3,
									(int) regs.retval);
						}
						break;

					case __NR_bind:
						regs.retval = lind_bind(regs.arg1,
								get_mem(regs.arg2,
										sizeof(struct lind_sockaddr)),
								regs.arg3);
						fprintf(stdout, "[monitor] bind(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_connect:
						regs.retval = lind_connect(regs.arg1,
								get_mem(regs.arg2,
										sizeof(struct lind_sockaddr)),
								regs.arg3);
						fprintf(stdout, "[monitor] connect(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);

						break;

					case __NR_accept:
						regs.retval = lind_accept(regs.arg1,
								get_mem(regs.arg2,
										sizeof(struct lind_sockaddr)),
								(lind_socklen_t*) regs.arg3);
						fprintf(stdout, "[monitor] accept(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_sendto:
						regs.retval = lind_sendto(regs.arg1,
								get_mem(regs.arg2, regs.arg3), regs.arg3,
								regs.arg4,
								get_mem(regs.arg2,
										sizeof(struct lind_sockaddr)),
								regs.arg5);
						fprintf(stdout, "[monitor] sendto(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_recvfrom: {
						var = malloc(regs.arg3);
						struct lind_sockaddr * buff = malloc(regs.arg6);
						regs.retval = lind_recvfrom(regs.arg1, var, regs.arg3,
								regs.arg4, buff, (lind_socklen_t*) regs.arg6);
						set_mem(regs.arg2, var, regs.arg3);
						set_mem(regs.arg5, buff, regs.arg6);
						fprintf(stdout, "[monitor] recvfrom(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
					}
						break;

					case __NR_recvmsg: {
						struct lind_msghdr msg_orig;
						struct lind_msghdr* msg = (struct lind_msghdr*) get_mem(
								regs.arg2, sizeof(struct lind_msghdr));
						msg_orig = *msg;
						struct lind_iovec* iovs = (struct lind_iovec*) get_mem(
								msg->msg_iov,
								sizeof(struct lind_iovec) * msg->msg_iovlen);
						struct lind_iovec* iovs_orig =
								(struct lind_iovec*) malloc(
										sizeof(struct lind_iovec)
												* msg->msg_iovlen);
						memcpy(iovs_orig, iovs,
								sizeof(struct lind_iovec) * msg->msg_iovlen);
						for (int i = 0; i < msg->msg_iovlen; ++i) {
							//iovs[i].iov_base = get_mem(iovs[i].iov_base, iovs[i].iov_len);
							iovs[i].iov_base = malloc(iovs[i].iov_len);
						}
						msg->msg_iov = iovs;
						//msg->msg_name = get_mem(msg->msg_name, msg->msg_namelen);
						msg->msg_name = malloc(msg->msg_namelen);
						//msg->msg_control = get_mem(msg->msg_control, msg->msg_controllen);
						msg->msg_control = malloc(msg->msg_controllen);

						regs.retval = lind_recvmsg(regs.arg1, msg, regs.arg3);

						set_mem(msg_orig.msg_name, msg->msg_name,
								msg->msg_namelen);
						set_mem(msg_orig.msg_control, msg->msg_control,
								msg->msg_controllen);
						for (int i = 0; i < msg->msg_iovlen; ++i) {
							set_mem(iovs_orig[i].iov_base, iovs[i].iov_base,
									iovs_orig[i].iov_len);
						}
						free(iovs);
						free(iovs_orig);
						free(msg);
						fprintf(stdout, "[monitor] recvmsg(%ld) = %d \n", regs.arg1,
								regs.retval);
					}
						break;

					case __NR_sendmsg: {

						struct lind_msghdr* msg = (struct lind_msghdr*) get_mem(
								regs.arg2, sizeof(struct lind_msghdr));

						struct lind_iovec* iovs = (struct lind_iovec*) get_mem(
								msg->msg_iov,
								sizeof(struct lind_iovec) * msg->msg_iovlen);
						struct lind_iovec* iovs_orig =
								(struct lind_iovec*) malloc(
										sizeof(struct lind_iovec)
												* msg->msg_iovlen);
						memcpy(iovs_orig, iovs,
								sizeof(struct lind_iovec) * msg->msg_iovlen);
						for (int i = 0; i < msg->msg_iovlen; ++i) {
							iovs[i].iov_base = get_mem(iovs[i].iov_base,
									iovs[i].iov_len);
						}
						msg->msg_iov = iovs;
						//msg->msg_name = get_mem(msg->msg_name, msg->msg_namelen);
						msg->msg_name = malloc(msg->msg_namelen);
						//msg->msg_control = get_mem(msg->msg_control, msg->msg_controllen);
						msg->msg_control = malloc(msg->msg_controllen);

						regs.retval = lind_sendmsg(regs.arg1, msg, regs.arg3);
						fprintf(stdout, "[monitor] sendmsg(%ld) = %d \n", regs.arg1,
								regs.retval);
					}
						break;

					case __NR_getsockname: {
						struct lind_sockaddr *buff = malloc(regs.arg3);

						regs.retval = lind_getsockname(regs.arg1, buff,
								(lind_socklen_t*) regs.arg3);
						set_mem(regs.arg2, buff, sizeof(struct lind_sockaddr));
						fprintf(stdout, "[monitor] getsockname(%ld) = %d \n", regs.arg1,
								regs.retval);
					}
						break;

					case __NR_getsockopt: {
						struct lind_sockaddr *buff = malloc(regs.arg2);
						regs.retval = lind_getsockopt(regs.arg1, regs.arg2,
								regs.arg3, buff, (lind_socklen_t*) regs.arg5);
						set_mem(regs.arg4, buff, sizeof(struct lind_sockaddr));
						fprintf(stdout, "[monitor] getsockopt(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
					}
						break;

					case __NR_setsockopt:
						regs.retval = lind_setsockopt(regs.arg1, regs.arg2,
								regs.arg3,
								get_mem(regs.arg4,
										sizeof(struct lind_sockaddr)),
										(lind_socklen_t) regs.arg5);
						fprintf(stdout, "[monitor] setsockopt(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					case __NR_socketpair:
						regs.retval = lind_socketpair(regs.arg1, regs.arg2,
								 get_mem(regs.arg3, 2 * sizeof(int)), regs.arg3);
						fprintf(stdout, "[monitor] socketpair(%d, %d) = %d \n",
								(int) regs.arg1, (int) regs.arg2, (int) regs.retval);
						break;

					case __NR_getpeername: {
						struct lind_sockaddr *buff = malloc(regs.arg3);
						regs.retval = lind_getpeername(regs.arg1, buff,
								(lind_socklen_t*) regs.arg3);
						set_mem(regs.arg2, buff, sizeof(struct lind_sockaddr));

						fprintf(stdout, "[monitor] getpeername(%d) = %d \n", (int) regs.arg1,
								 (int) regs.retval);
					}
						break;

					case __NR_select: {
						void* set1 = get_mem(regs.arg2, sizeof(fd_set));
						void* set2 = get_mem(regs.arg3, sizeof(fd_set));
						void* set3 = get_mem(regs.arg4, sizeof(fd_set));
						void* tv = get_mem(regs.arg5, sizeof(struct timeval));
						regs.retval = lind_select(regs.arg1, set1, set2, set3,
								tv);
						set_mem(regs.arg2, set1, sizeof(fd_set));
						set_mem(regs.arg3, set2, sizeof(fd_set));
						set_mem(regs.arg4, set3, sizeof(fd_set));
						set_mem(regs.arg5, tv, sizeof(struct timeval));
						fprintf(stdout, "[monitor] select(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
					}
						break;

					case __NR_poll: {
						struct lind_pollfd * lpfd = malloc(
								sizeof(struct lind_pollfd));
						regs.retval = lind_poll(lpfd, regs.arg2, regs.arg3);
						set_mem(regs.arg1, lpfd, sizeof(struct lind_pollfd));
						fprintf(stdout, "[monitor] poll(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
					}
						break;

					case __NR_epoll_ctl: {
						struct lind_epoll_event *event = malloc(
								sizeof(struct lind_epoll_event));
						regs.retval = lind_epoll_ctl(regs.arg1, regs.arg2,
								regs.arg3,
								get_mem(regs.arg4,
										sizeof(struct lind_epoll_event)));

						set_mem(regs.arg4, event,
								sizeof(struct lind_epoll_event));
						fprintf(stdout, "[monitor] epoll_ctl(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
					}
						break;

					case __NR_epoll_wait: {
						struct lind_epoll_event *event = malloc(
								sizeof(struct lind_epoll_event));
						regs.retval = lind_epoll_wait(regs.arg1, event,
								regs.arg3, regs.arg4);
						set_mem(regs.arg2, event,
								sizeof(struct lind_epoll_event));
					}
						fprintf(stdout, "[monitor] epoll_wait(%d) = %d \n", (int) regs.arg1,
								(int) regs.retval);
						break;

					default:
						regs.retval = EINVAL;
						fprintf(stdout, "[monitor] %s() = %d is not supported by Lind \n", syscall_names[syscall_num], regs.retval);
						break;
					} /* switch*/
					set_args(&regs);
				}
			} /* entering */
		} /* WSTOPSIG*/
	}/* while */
}

/* get the path of files required by a syscall through the defined address */
char *get_path(uintptr_t addr)
{
	size_t len= PATH_MAX;
	char *buffer =  buffer = malloc(len);

	uint32_t tmp;
	int i = 0;

	while (1) {
		if (i >= len) {
			len *= 2;
			buffer = realloc(buffer, len);
		}

		tmp = ptrace(PTRACE_PEEKDATA, tracee, addr + i, NULL);
		memcpy(buffer + i, (void *) &tmp, sizeof(tmp));

		if (memchr(&tmp, 0, sizeof(tmp)) != NULL) {
			break;
		}

		i += 4;
	}

	return buffer;
}

/* set the memory from an address to a specific buffer */
void set_mem(uintptr_t addr, void * buff, size_t count)
{
	long ret = -1;
	int i;

	int fullblocks = count / sizeof(long);
	int remainder = count % sizeof(long);

	for (i = 0; i < fullblocks; i++) {
		ret = ptrace(PTRACE_POKEDATA, tracee,
				(char *) (addr + sizeof(long) * i),
				*(long*) ((char*) buff + sizeof(long) * i));
	}

	if (remainder) {
		unsigned long value = ptrace(PTRACE_PEEKDATA, tracee,
				(char *) (addr + sizeof(long) * fullblocks), 0);
		value = (ret & (ULONG_MAX << (remainder * 8)))
				| (*(long*) ((char*) buff + sizeof(long) * fullblocks)
						& (~(ULONG_MAX << (remainder * 8))));
		ret = ptrace(PTRACE_POKEDATA, tracee,
				(char *) (addr + sizeof(long) * fullblocks), value);
	}

}

/* get count number of memory defined through an address */
void *get_mem(uintptr_t addr, size_t count)
{

	long ret;
	int i;
	long *mem = malloc((count / sizeof(long) + 1) * sizeof(long));

	for (i = 0; i < count / sizeof(long) + 1; i++) {
		ret = ptrace(PTRACE_PEEKDATA, tracee,
				(char *) (addr + sizeof(long) * i), 0);
		mem[i] = ret;
	}

	return (void*) mem;
}

/* return syscall number by name */
int get_syscall_num(char *name)
{
	int i;

	for (i = 0; i < TOTAL_SYSCALLS; i++)
		if (syscall_names[i] && !strcmp(syscall_names[i], name))
			return i;
	return -1;
}

/* return the arguments of a syscall by ptrace */
void get_args(struct syscall_args *args)
{

	if (ptrace(PTRACE_GETREGS, tracee, 0, &args->user) < 0) {
		fprintf(stderr, "ptrace did not get the register arguments.");
		return;
	}
	args->syscall = args->user.regs.orig_rax;
	args->retval = args->user.regs.rax;

	args->arg1 = args->user.regs.rdi;
	args->arg2 = args->user.regs.rsi;
	args->arg3 = args->user.regs.rdx;
	args->arg4 = args->user.regs.r10;
	args->arg5 = args->user.regs.r8;
	args->arg6 = args->user.regs.r9;

}

/* set the arguments of a syscall by ptrace */
void set_args(struct syscall_args *args)
{
	args->user.regs.orig_rax = args->syscall;
	args->user.regs.rax = args->retval;
	args->user.regs.rdi = args->arg1;
	args->user.regs.rsi = args->arg2;
	args->user.regs.rdx = args->arg3;
	args->user.regs.r10 = args->arg4;
	args->user.regs.r8 = args->arg5;
	args->user.regs.r9 = args->arg6;

	if (ptrace(PTRACE_SETREGS, tracee, 0, &args->user) < 0) {
		fprintf(stderr, "ptrace was not set. \n");
		return;
	}
}

/* load the config file containing the policies to dispatch the syscalls */
int load_config()
{
	char *str, buff[100];
	char *key, *value;
	enum monitor_action mact = ALLOW_LIND;
	const char * config_file = get_lind_config();

	FILE *fp = fopen(config_file, "r");
	if (fp == NULL) {
		fprintf(stdout, "[monitor] Config file %s could not be opened. \n ", config_file);
		exit(-1);
	}

	while ((str = fgets(buff, sizeof buff, fp)) != NULL) {

		if (buff[0] == '\n' || buff[0] == '#')
			continue;

		char* sep;
		if ((sep = strchr(str, '='))) {
			*sep++ = 0;
			key = strdup(str);
			value = strdup(sep);

			if (strcmp(value, "ALLOW_LIND\n") == 0) {
				mact = ALLOW_LIND;
			} else if (strcmp(value, "DENY_LIND\n") == 0) {
				mact = DENY_LIND;
			} else if (strcmp(value, "ALLOW_OS\n") == 0) {
				mact = ALLOW_OS;
			}

			int sys = get_syscall_num(key);
			monitor_actions[sys] = mact;
		}
	}

	fflush(fp);
	fclose(fp);

	return 0;
}
