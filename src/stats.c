#include "config.h"
#include "zglobal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

struct lrzsz_stat stat_for_file;
struct lrzsz_stat stat_for_all;

static FILE *journal_fp = NULL;

#define OUT(X,Y)   do { if (st->X) fprintf(out, " %s=%lu",Y,(unsigned long) (st->X)); } while(0)
#define OUTLL(X,Y) do { if (st->X) fprintf(out, " %s=%llu",Y,(unsigned long long) (st->X)); } while(0)

static void
fprintf_sanitized(FILE *out, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 0x20 && c < 0x7f)
            fputc(c, out);
        else
            fprintf(out, "\\x%02x", c);
    }
}

void
stats_set_filename(const char *fn)
{
    /* free a previous name, if any: the caller may set a new name without
     * accounting (stats_account_one frees it, but is not always called) */
    if (stat_for_file.filename) {
        free(stat_for_file.filename);
        stat_for_file.filename=NULL;
    }
    stat_for_file.filename=xstrdup(fn);
}

static void
write_stats_line(FILE *out, struct lrzsz_stat *st, bool forone)
{
    struct termios tty;
    struct timespec end_timespec;
    struct timespec wall_timespec;
    double dur=-1;
    int ret;
    const char *dir;
    if (!out) {
        return;
    }

    ret=clock_gettime(CLOCK_MONOTONIC, &end_timespec);
    if (!ret) {
        double end;
        end=end_timespec.tv_sec+((double)end_timespec.tv_nsec/1e9);
        if (st->start_time>0) {
            dur=end-st->start_time;
        }
    }
    /* no summary for one file. */
    if (!forone && st->files_ok+st->files_err+st->policy_rejects+st->skipped < 2) {
        return;
    }

    if (clock_gettime(CLOCK_REALTIME, &wall_timespec) == 0)
        fprintf(out,"ts=%ld.%03ld",(long)wall_timespec.tv_sec, wall_timespec.tv_nsec/1000000);
    else
        fprintf(out,"ts=%ld",(long)time(NULL));

    fprintf(out," prog=");
    fprintf_sanitized(out, lrzsz_progname);
    dir = (st->direction == 's') ? "tx" : "rx";

    fprintf(out," dir=%s", dir);
    fprintf(out," proto=%c", protocol == ZM_XMODEM ? 'x' : protocol == ZM_YMODEM ? 'y' : 'z');
    if (forone) {
        fprintf(out," filename=");
        fprintf_sanitized(out, st->filename ? st->filename : "unknown"); /* unknown: coding error or out of memory. */
        if (st->files_ok) {
            fprintf(out," status=ok");
        } else if (st->files_err) {
            fprintf(out," status=failed");
        } else if (st->local_errors) {
            fprintf(out," status=error");
        } else if (st->policy_rejects) {
            fprintf(out," status=policy");
        } else if (st->skipped) {
            fprintf(out," status=skipped");
        } else {
            fprintf(out," status=unknown");
        }
    } else {
        OUT(files_ok,"files_ok");
        OUT(files_err,"files_err");
        OUT(policy_rejects,"policy");
        OUT(skipped,"skipped");
    }

    OUT(valid_messages,"valid_msgs");
    OUT(invalid_messages,"invalid_msgs");
    fprintf(out," total_msg_bytes=%llu", st->total_message_bytes);
    if (st->valid_messages+st->invalid_messages) {
        fprintf(out," avg_msg_size=%llu", st->total_message_bytes/(st->valid_messages+st->invalid_messages));
    }
    OUT(out_of_sync_messages,"out_of_sync_msgs");
    OUT(local_errors,"local_errors");
    OUT(cancels,"cancels");
    OUT(timeouts,"timeouts");
    OUT(too_many_errors,"too_many_errors");
    OUT(format_errors,"format_errors");
    
    OUTLL(net_bytes_in,"net_bytes_in");
    OUT(net_read_ops,"net_read_ops");
    OUTLL(net_bytes_out,"net_bytes_out");
    OUT(net_write_ops,"net_write_ops");
    if (dur>0) {
        fprintf(out," dur=%.3f",dur);
        fprintf(out," bps=%.3f",(8.0*st->total_message_bytes)/dur);
    }

    if (0==tcgetattr(0,&tty)) {
        int ibaud = getspeed(cfgetispeed(&tty));
        if (0==tcgetattr(1,&tty)) {
            int obaud = getspeed(cfgetospeed(&tty));
            if (ibaud==obaud) {
                fprintf(out," baud=%d",ibaud);
            } else {
                fprintf(out," ibaud=%d obaud=%d",ibaud,obaud);
            }
        }
    }
    fprintf(out,"\n");
}


void
stats_log(struct lrzsz_stat *st, bool forone)
{
    setlocale(LC_NUMERIC,"C");
    write_stats_line(journal_fp, st, forone);

    setlocale(LC_NUMERIC,"");
}

#define TRANSFER(X) do { \
    stat_for_all.X += stat_for_file.X; \
    stat_for_file.X = 0; \
    } while(0)

void
stats_account_one(void)
{
#define debug_stats 0
#if debug_stats
    unsigned int i;
    unsigned char * todump = (unsigned char*)&stat_for_file;
    fprintf(stderr,"\nfile: ");
    for (i = 0; i < sizeof(stat_for_file); ++i) fprintf(stderr,"%02X ", todump[i]);
    todump = (unsigned char*)&stat_for_all;
    fprintf(stderr,"\nall0: ");
    for (i = 0; i < sizeof(stat_for_all); ++i) fprintf(stderr,"%02X ", todump[i]);
    fprintf(stderr,"\n");
#endif

    stats_log(&stat_for_file,true);

    if (stat_for_file.filename) {
        free(stat_for_file.filename);
        stat_for_file.filename=NULL;
    }
    TRANSFER(valid_messages); 
    TRANSFER(invalid_messages); 
    TRANSFER(out_of_sync_messages); 
    TRANSFER(format_errors); 

    TRANSFER(cancels); 
    TRANSFER(timeouts); 
    TRANSFER(too_many_errors); 

    TRANSFER(files_ok); 
    TRANSFER(files_err); 
    TRANSFER(local_errors); 
    TRANSFER(policy_rejects); 
    TRANSFER(skipped); 

    TRANSFER(total_message_bytes); 
    TRANSFER(net_read_ops); 
    TRANSFER(net_bytes_in); 
    TRANSFER(net_write_ops); 
    TRANSFER(net_bytes_out); 

#if debug_stats
    fprintf(stderr,"all1: ");
    for (i = 0; i < sizeof(stat_for_all); ++i) fprintf(stderr,"%02X ", todump[i]);
    fprintf(stderr,"\n");
#endif
}

void
stats_account_finalize_file(void)
{
    TRANSFER(valid_messages);
    TRANSFER(invalid_messages);
    TRANSFER(out_of_sync_messages);
    TRANSFER(format_errors);

    TRANSFER(cancels);
    TRANSFER(timeouts);
    TRANSFER(too_many_errors);

    TRANSFER(files_ok);
    TRANSFER(files_err);
    TRANSFER(local_errors);
    TRANSFER(policy_rejects);
    TRANSFER(skipped);

    TRANSFER(total_message_bytes);
    TRANSFER(net_read_ops);
    TRANSFER(net_bytes_in);
    TRANSFER(net_write_ops);
    TRANSFER(net_bytes_out);
}

void
stats_start_one(void)
{
    struct timespec start;
    int ret;

    ret=clock_gettime(CLOCK_MONOTONIC, &start);
    if (ret) {
        stat_for_file.start_time=0;
    } else {
        stat_for_file.start_time=start.tv_sec+((double)start.tv_nsec/1e9);
    }
}

void
stats_journal_open(const char *path)
{
    int fd;
    if (journal_fp) {
        fclose(journal_fp);
        journal_fp = NULL;
    }
    fd = open(path, O_WRONLY|O_CREAT|O_APPEND, 0600);
    if (fd < 0) {
        lrzsz_warning(errno,_("%s: cannot open journal file '%s'"), lrzsz_progname, path);
        return;
    }
    journal_fp = fdopen(fd, "a");
    if (!journal_fp) {
        lrzsz_warning(errno,_("%s: cannot open journal file '%s'"), lrzsz_progname, path);
        close(fd);
        return;
    }
    setvbuf(journal_fp, NULL, _IOLBF, 0);
}

void
stats_journal_close(void)
{
    if (journal_fp) {
        fclose(journal_fp);
        journal_fp = NULL;
    }
}
