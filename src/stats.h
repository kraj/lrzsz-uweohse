#ifndef LRZSZ_STATS_H
#define LRZSZ_STATS_H

struct lrzsz_stat {
    char *filename;
    double start_time;

    /* a message is a sector/subframe/packet, something with an own checksum. */
    unsigned long valid_messages;
    unsigned long invalid_messages;
    unsigned long out_of_sync_messages; /* a valid message out of sync */
    /* we were reading a message, had identified it as such, but could not find its end */
    unsigned long format_errors;

    /* what happened to files: */
    unsigned long files_ok;
    unsigned long files_err;
    unsigned long local_errors;
    unsigned long policy_rejects;
    unsigned long skipped;
    unsigned long cancels; /* a transfer was cancelled after one side began to send data. */

    /* general error handling */
    unsigned long timeouts; /* timeout, read error */
    unsigned long too_many_errors; /* some operation failed because of that */

    /* counters */
    unsigned long long total_message_bytes;
    unsigned long long net_bytes_in;
    unsigned long net_read_ops;
    unsigned long long net_bytes_out;
    unsigned long net_write_ops;

    char direction; /* 'r' = receive (lrz), 's' = send (lsz) */
};
extern struct lrzsz_stat stat_for_file;
extern struct lrzsz_stat stat_for_all;

void stats_account_one(void);
void stats_start_one(void);
void stats_log(struct lrzsz_stat *st, bool forone);
void stats_set_filename(const char *);
void stats_journal_open(const char *path);
void stats_journal_close(void);

#endif
