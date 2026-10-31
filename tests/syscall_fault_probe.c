/* Test-only ptrace observer/injector for real raw-syscall loops. Hosted, not shipped. */
#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t op,pad[3]; uint32_t arch; uint64_t ip,sp;
    union { struct {uint64_t nr,args[6];} entry;
            struct {int64_t rval;uint8_t is_error;} exit; } u;
} ProbeInfo;
static int wait_child(pid_t pid,int *status){
    pid_t r;do{r=waitpid(pid,status,0);}while(r<0&&errno==EINTR);return r==pid;
}
int main(int argc,char **argv){
    if(argc<2)return 64;
    pid_t child=fork();if(child<0)return 1;
    if(!child){
        if(ptrace(PTRACE_TRACEME,0,0,0)<0)_exit(77);
        raise(SIGSTOP);execv(argv[1],argv+1);_exit(126);
    }
    int status=0;
    if(!wait_child(child,&status))return 1;
    if(WIFEXITED(status)&&WEXITSTATUS(status)==77)return 77;
    if(!WIFSTOPPED(status))return 1;
    long opts=PTRACE_O_TRACESYSGOOD|PTRACE_O_TRACEEXEC|PTRACE_O_EXITKILL;
    if(ptrace(PTRACE_SETOPTIONS,child,0,opts)<0){kill(child,SIGKILL);wait_child(child,&status);return 77;}
    unsigned long long execs=0,read_eintr=0,write_eintr=0,short_reads=0,short_writes=0,forbidden=0;
    int injected=0,target_exit=-1,deliver=0,shortened=0;
    unsigned long long original_rdx=0;
    for(;;){
        if(ptrace(PTRACE_SYSCALL,child,0,deliver)<0)goto fail;
        deliver=0;if(!wait_child(child,&status))goto fail;
        if(WIFEXITED(status)){target_exit=WEXITSTATUS(status);break;}
        if(WIFSIGNALED(status)){target_exit=-WTERMSIG(status);break;}
        if(!WIFSTOPPED(status))goto fail;
        unsigned event=(unsigned)status>>16;
        int sig=WSTOPSIG(status);
        if(event==PTRACE_EVENT_EXEC){execs++;continue;}
        if(sig!=(SIGTRAP|0x80)){
            if(sig!=SIGTRAP&&sig!=SIGSTOP)deliver=sig;
            continue;
        }
        ProbeInfo info;memset(&info,0,sizeof(info));
        if(ptrace(PTRACE_GET_SYSCALL_INFO,child,sizeof(info),&info)<0)goto fail;
        struct user_regs_struct regs;
        if(ptrace(PTRACE_GETREGS,child,0,&regs)<0)goto fail;
        if(info.op==1){
            uint64_t nr=info.u.entry.nr;
            if(execs&&(nr==56||nr==57||nr==58||nr==322||nr==435||nr==59)){
                forbidden++;regs.orig_rax=(unsigned long long)-1;injected=2;
                if(ptrace(PTRACE_SETREGS,child,0,&regs)<0)goto fail;
            }else if(execs&&((nr==0&&!read_eintr)||(nr==1&&!write_eintr))){
                if(nr==0)read_eintr++;else write_eintr++;
                regs.orig_rax=(unsigned long long)-1;injected=1;
                if(ptrace(PTRACE_SETREGS,child,0,&regs)<0)goto fail;
            }else if(execs&&(nr==0||nr==1)&&regs.rdx>13){
                original_rdx=regs.rdx;shortened=1;
                regs.rdx=13;if(nr==0)short_reads++;else short_writes++;
                if(ptrace(PTRACE_SETREGS,child,0,&regs)<0)goto fail;
            }
        }else if(info.op==2&&(injected||shortened)){
            if(injected)regs.rax=(unsigned long long)(injected==1?-EINTR:-EPERM);
            /* A real short syscall preserves the user request register. */
            if(shortened)regs.rdx=original_rdx;
            injected=0;shortened=0;
            if(ptrace(PTRACE_SETREGS,child,0,&regs)<0)goto fail;
        }
    }
    fprintf(stderr,"{\"syscall_probe\":true,\"execs\":%llu,\"forbidden\":%llu,\"read_eintr\":%llu,\"write_eintr\":%llu,\"short_reads\":%llu,\"short_writes\":%llu,\"target_exit\":%d}\n",execs,forbidden,read_eintr,write_eintr,short_reads,short_writes,target_exit);
    return !(target_exit==0&&execs==1&&!forbidden&&read_eintr==1&&write_eintr==1&&short_reads&&short_writes);
fail:
    perror("ptrace probe");kill(child,SIGKILL);wait_child(child,&status);return 1;
}
