/*
*****************************************************************************
*
*  File: psadwatchd.c
*
*  Purpose: psadwatchd checks on an interval of every five seconds to make
*           sure that both kmsgsd and psad are running on the box.  If
*           either daemon has died, psadwatchd will restart it and notify
*           each email address in @email_addresses that the daemon has been
*           restarted.
*
*  Author: Michael Rash (mbr@cipherdyne.com)
*
*  Credits:  (see the CREDITS file)
*
*  Version: 1.1.1
*
*  Copyright (C) 1999-2001 Michael Rash (mbr@cipherdyne.com)
*
*  License (GNU Public License):
*
*     This program is distributed in the hope that it will be useful,
*     but WITHOUT ANY WARRANTY; without even the implied warranty of
*     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*     GNU General Public License for more details.
*
*     You should have received a copy of the GNU General Public License
*     along with this program; if not, write to the Free Software
*     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
*     USA
*
*****************************************************************************
*
*  $Id$
*/

/* INCLUDES *****************************************************************/
#include "psad.h"

/* GLOBALS ******************************************************************/
short int psad_syscalls_ctr     = 0;
short int kmsgsd_syscalls_ctr   = 0;
short int diskmond_syscalls_ctr = 0;
const char mail_redr[] = " < /dev/null > /dev/null 2>&1";
const char hostname[] = HOSTNAME;
char mail_addrs[MAX_GEN_LEN];
char shCmd[MAX_GEN_LEN];
char mailCmd[MAX_GEN_LEN];

/* PROTOTYPES ***************************************************************/
static void parse_config(
    char *config_file,
    char *psad_binary,
    char *psad_pid_file,
    char *psad_cmdline_file,
    char *kmsgsd_binary,
    char *kmsgsd_pid_file,
    char *diskmond_binary,
    char *diskmond_pid_file,
    char *shCmd,
    char *mailCmd,
    char *mail_addrs,
    char *psadwatchd_pid_file,
    unsigned int *psadwatchd_check_interval,
    unsigned int *psadwatchd_max_retries
);
static void check_process(
    const char *pid_name,
    const char *pid_file,
    const char *cmdline_file,
    const char *binary_path,
    unsigned int max_retries
);
static void incr_syscall_ctr(const char *pid_name, unsigned int max_retries);
static void reset_syscall_ctr(const char *pid_name);
static void give_up(const char *pid_name);
static void exec_binary(const char *binary_path, const char *cmdline_file);

/* MAIN *********************************************************************/
int main(int argc, char *argv[]) {
    char config_file[MAX_PATH_LEN];
    char psadCmd[MAX_PATH_LEN];
    char psad_pid_file[MAX_PATH_LEN];
    char psad_cmdline_file[MAX_PATH_LEN];
    char kmsgsdCmd[MAX_PATH_LEN];
    char kmsgsd_pid_file[MAX_PATH_LEN];
    char diskmondCmd[MAX_PATH_LEN];
    char diskmond_pid_file[MAX_PATH_LEN];
    char psadwatchd_pid_file[MAX_PATH_LEN];
    unsigned int psadwatchd_check_interval = 5;  /* default to 5 seconds */
    unsigned int psadwatchd_max_retries = 10; /* default to 10 tries */
    time_t config_mtime;
    struct stat statbuf;
    int len;

#ifdef DEBUG
    printf(" .. Entering DEBUG mode ..n");
    sleep(1);
#endif

    /* handle command line arguments */
    if (argc == 1) {  /* nothing but the program name was
                         specified on the command line */
        len = strlen(CONFIG_FILE);
        if (len > MAX_PATH_LEN)
            len = MAX_PATH_LEN;
        memcpy(config_file, CONFIG_FILE, len);
        config_file[len] = '\0';
    } else if (argc == 2) {  /* the path to the config file was
                                supplied on the command line */
        len = strlen(argv[1]);
        if (len > MAX_PATH_LEN)
            len = MAX_PATH_LEN;
        memcpy(config_file, argv[1], len);
        config_file[len] = '\0';
    } else {
        printf(" .. You may only specify the path to a single config file:  ");
        printf("Usage:  psadwatchd <configfile>\n");
        exit(EXIT_FAILURE);
    }

    if (stat(config_file, &statbuf)) {
        printf(" ** Could not get mtime for config file: %s\n",
            config_file);
        exit(EXIT_FAILURE);
    }

    /* initialize config_mtime */
    config_mtime = statbuf.st_mtime;


#ifdef DEBUG
    printf(" .. parsing config_file: %s\n", config_file);
#endif

    /* parse the config file */
    parse_config(
        config_file,
        psadCmd,
        psad_pid_file,
        psad_cmdline_file,
        kmsgsdCmd,
        kmsgsd_pid_file,
        diskmondCmd,
        diskmond_pid_file,
        shCmd,
        mailCmd,
        mail_addrs,
        psadwatchd_pid_file,
        &psadwatchd_check_interval,
        &psadwatchd_max_retries
    );

    /* first make sure there isn't another psadwatchd already running */
    check_unique_pid(psadwatchd_pid_file, "psadwatchd");

#ifndef DEBUG
    /* become a daemon */
    daemonize_process(psadwatchd_pid_file);
#endif

    /* start doing the real work now that the daemon is running and
     * the config file has been processed */

    /* MAIN LOOP: */
    for (;;) {
        check_process("psad", psad_pid_file, psad_cmdline_file,
            psadCmd, psadwatchd_max_retries);
        check_process("kmsgsd", kmsgsd_pid_file, NULL,
            kmsgsdCmd, psadwatchd_max_retries);
        check_process("diskmond", diskmond_pid_file, NULL,
            diskmondCmd, psadwatchd_max_retries);

        /* check to see if we need to re-import the config file */
        if (check_import_config(&config_mtime, config_file)) {
#ifdef DEBUG
    printf(" .. re-parsing config file: %s\n", config_file);
#endif
            /* reparse the config file since it was updated */
            parse_config(
                config_file,
                psadCmd,
                psad_pid_file,
                psad_cmdline_file,
                kmsgsdCmd,
                kmsgsd_pid_file,
                diskmondCmd,
                diskmond_pid_file,
                shCmd,
                mailCmd,
                mail_addrs,
                psadwatchd_pid_file,
                &psadwatchd_check_interval,
                &psadwatchd_max_retries
            );
        }

        sleep(psadwatchd_check_interval);
    }

    /* this statement doesn't get executed, but for completeness... */
    exit(EXIT_SUCCESS);
}
/******************** end main ********************/

static void check_process(
    const char *pid_name,
    const char *pid_file,
    const char *cmdline_file,
    const char *binary_path,
    unsigned int max_retries)
{
    FILE *pidfile_ptr;
    pid_t pid;
    char mail_str[MAX_MSG_LEN] = "";
    char pid_line[MAX_PID_SIZE];

    if ((pidfile_ptr = fopen(pid_file, "r")) == NULL) {
        /* the pid file must not exist (or we can't read it), so
         * start the appropriate process and return */
#ifdef DEBUG
    printf(" .. Could not open pid_file: %s\n", pid_file);
#endif
        strlcat(mail_str, " -s \" ** psadwatchd: Restarting ", MAX_MSG_LEN);
        strlcat(mail_str, pid_name, MAX_MSG_LEN);
        strlcat(mail_str, " on ", MAX_MSG_LEN);
        strlcat(mail_str, hostname, MAX_MSG_LEN);
        strlcat(mail_str, "\" ", MAX_MSG_LEN);
        strlcat(mail_str, mail_addrs, MAX_MSG_LEN);
        strlcat(mail_str, mail_redr, MAX_MSG_LEN);

#ifdef DEBUG
    printf("sending mail:  %s\n", mail_str);
#endif
        /* send the email */
        send_alert_email(shCmd, mailCmd, mail_str);

        /* restart the process */
        exec_binary(binary_path, cmdline_file);

        /* increment the number of times we have tried to restart the binary */
        incr_syscall_ctr(pid_name, max_retries);
        return;
    }

    /* read the first line of the pid_file, which will contain the
     * process id of any running pid_name process. */
    if (fgets(pid_line, MAX_PID_SIZE, pidfile_ptr) == NULL) {
#ifdef DEBUG
    printf(" .. Could not read the pid_file: %s\n", pid_file);
#endif
        return;
    }

    /* convert the pid_line into an integer */
    pid = atoi(pid_line);

    /* close the pid_file now that we have read it */
    fclose(pidfile_ptr);

    if (kill(pid, 0) != 0) {  /* the process is not running so start it */
#ifdef DEBUG
        printf(" .. executing exec_binary(%s)\n", binary_path);
#endif
        //strlcat(mail_str, mailCmd, MAX_MSG_LEN);
        strlcat(mail_str, " -s \" ** psadwatchd: Restarting ", MAX_MSG_LEN);
        strlcat(mail_str, pid_name, MAX_MSG_LEN);
        strlcat(mail_str, " on ", MAX_MSG_LEN);
        strlcat(mail_str, hostname, MAX_MSG_LEN);
        strlcat(mail_str, "\" ", MAX_MSG_LEN);
        strlcat(mail_str, mail_addrs, MAX_MSG_LEN);
        strlcat(mail_str, mail_redr, MAX_MSG_LEN);

#ifdef DEBUG
    // printf("sending mail:  %s\n", mail_str);
#endif
        /* send the email */
        send_alert_email(shCmd, mailCmd, mail_str);

        /* execute the binary_path psad daemon */
        exec_binary(binary_path, cmdline_file);

        /* increment the number of times we have tried to restart the binary */
        incr_syscall_ctr(pid_name, max_retries);
    } else {
#ifdef DEBUG
        printf(" .. %s is running.\n", pid_name);
#endif
        reset_syscall_ctr(pid_name); /* reset the syscall counter */
    }
    return;
}

static void incr_syscall_ctr(const char *pid_name, unsigned int max_retries)
{
    if (strcmp("psad", pid_name) == 0) {
        psad_syscalls_ctr++;
#ifdef DEBUG
        printf(" .. %s not running.  Trying to restart (%d tries so far).\n",
            pid_name, psad_syscalls_ctr);
#endif
        if (psad_syscalls_ctr >= max_retries)
            give_up(pid_name);
    } else if (strcmp("diskmond", pid_name) == 0) {
        diskmond_syscalls_ctr++;
#ifdef DEBUG
        printf(" .. %s not running.  Trying to restart (%d tries so far).\n",
            pid_name, diskmond_syscalls_ctr);
#endif
        if (diskmond_syscalls_ctr >= max_retries)
            give_up(pid_name);
    } else if (strcmp("kmsgsd", pid_name) == 0) {
        kmsgsd_syscalls_ctr++;
#ifdef DEBUG
        printf(" .. %s not running.  Trying to restart (%d tries so far).\n",
            pid_name, kmsgsd_syscalls_ctr);
#endif
        if (kmsgsd_syscalls_ctr >= max_retries)
            give_up(pid_name);
    }
    return;
}

static void reset_syscall_ctr(const char *pid_name)
{
    if (strcmp("psad", pid_name) == 0) {
        psad_syscalls_ctr = 0;
    } else if (strcmp("diskmond", pid_name) == 0) {
        diskmond_syscalls_ctr = 0;
    } else if (strcmp("kmsgsd", pid_name) == 0) {
        kmsgsd_syscalls_ctr = 0;
    }
    return;
}

static void give_up(const char *pid_name)
{
    char mail_str[MAX_MSG_LEN] = "";
#ifdef DEBUG
    printf(" ** Could not restart %s process.  Exiting.\n", pid_name);
#endif
    strlcat(mail_str, " -s \"** psadwatchd: Could not restart ", MAX_MSG_LEN);
    strlcat(mail_str, pid_name, MAX_MSG_LEN);
    strlcat(mail_str, " on ", MAX_MSG_LEN);
    strlcat(mail_str, hostname, MAX_MSG_LEN);
    strlcat(mail_str, ".  Exiting.\" ", MAX_MSG_LEN);
    strlcat(mail_str, mail_addrs, MAX_MSG_LEN);
    strlcat(mail_str, mail_redr, MAX_MSG_LEN);

    /* Send the email */
    send_alert_email(shCmd, mailCmd, mail_str);
    exit(EXIT_FAILURE);
}

static void exec_binary(const char *binary, const char *cmdlinefile)
{
    FILE *cmdline_ptr;
    char *prog_argv[MAX_ARG_LEN];
    char cmdline_buf[MAX_LINE_BUF];
    char *index;
    pid_t child_pid;
    int arg_num=0, non_ws, i;

    prog_argv[arg_num] = (char *) malloc(strlen(binary));
    if (prog_argv[arg_num] == NULL) {
        exit(EXIT_FAILURE);
    }
    strlcpy(prog_argv[arg_num], binary, MAX_ARG_LEN);
    arg_num++;

    if (cmdlinefile != NULL) {
        /* restart binary with its command line arguments intact */
        if ((cmdline_ptr = fopen(cmdlinefile, "r")) == NULL) {
            exit(EXIT_FAILURE);
        }
        if ((fgets(cmdline_buf, MAX_LINE_BUF, cmdline_ptr)) == NULL) {
            exit(EXIT_FAILURE);
        }
        fclose(cmdline_ptr);

        /* initialize index to the beginning of the line */
        index = cmdline_buf;

        /* advance the index pointer through any whitespace
         * at the beginning of the line */
        while (*index == ' ' || *index == '\t') index++;

        while (*index != '\n' && *index != '\0') {
            non_ws = 0;
            while (*index != ' ' && *index != '\t'
                    && index != '\0' && *index != '\n') {
                index++;
                non_ws++;
            }
            prog_argv[arg_num] = (char *) malloc(non_ws+1);
            if (prog_argv[arg_num] == NULL) {
                exit(EXIT_FAILURE);
            }
            for (i=0; i<non_ws; i++)
                prog_argv[arg_num][i] = *(index - (non_ws - i));
            prog_argv[arg_num][i] = '\0';

            arg_num++;

            /* get past any whitespace */
            while (*index == ' ' || *index == '\t') index++;
        }
    }
    /* is it necessary to malloc for the ending NULL? */
    prog_argv[arg_num] = (char *) malloc(1);
    if (prog_argv[arg_num] == NULL) {
        exit(EXIT_FAILURE);
    }
    prog_argv[arg_num] = NULL;

    if ((child_pid = fork()) < 0)
        /* could not fork */
        exit(EXIT_FAILURE);
    else if (child_pid > 0) {
        wait(NULL);
        for (i=0; i<=arg_num; i++) {
            free(prog_argv[i]);
        }
    } else {
#ifdef DEBUG
        printf(" .. restarting %s\n", binary);
#endif
        execve(binary, prog_argv, NULL);  /* don't use environment */
    }
    return;
}

static void parse_config(
    char *config_file,
    char *psadCmd,
    char *psad_pid_file,
    char *psad_cmdline_file,
    char *kmsgsdCmd,
    char *kmsgsd_pid_file,
    char *diskmondCmd,
    char *diskmond_pid_file,
    char *shCmd,
    char *mailCmd,
    char *mail_addrs,
    char *psadwatchd_pid_file,
    unsigned int *psadwatchd_check_interval,
    unsigned int *psadwatchd_max_retries)
{
    FILE *config_ptr;         /* FILE pointer to the config file */
    int linectr = 0;
    char config_buf[MAX_LINE_BUF];
    char char_psadwatchd_check_interval[MAX_NUM_LEN];
    char char_psadwatchd_max_retries[MAX_NUM_LEN];
    char *index;

    if ((config_ptr = fopen(config_file, "r")) == NULL) {
        perror(" ** Could not open config file");
        exit(EXIT_FAILURE);
    }

    /* increment through each line of the config file */
    while ((fgets(config_buf, MAX_LINE_BUF, config_ptr)) != NULL) {
        linectr++;
        index = config_buf;  /* set the index pointer to the
                                beginning of the line */

        /* advance the index pointer through any whitespace
         * at the beginning of the line */
        while (*index == ' ' || *index == '\t') index++;

        /* skip comments and blank lines, etc. */
        if ((*index != '#') && (*index != '\n') &&
                (*index != ';') && (index != NULL)) {

            find_char_var("psadCmd ", psadCmd, index);
            find_char_var("PSAD_PID_FILE ", psad_pid_file, index);
            find_char_var("PSAD_CMDLINE_FILE ", psad_cmdline_file, index);
            find_char_var("kmsgsdCmd ", kmsgsdCmd, index);
            find_char_var("KMSGSD_PID_FILE ", kmsgsd_pid_file, index);
            find_char_var("diskmondCmd ", diskmondCmd, index);
            find_char_var("DISKMOND_PID_FILE ", diskmond_pid_file, index);
            find_char_var("shCmd ", shCmd, index);
            find_char_var("mailCmd ", mailCmd, index);
            find_char_var("EMAIL_ADDRESSES ", mail_addrs, index);
            find_char_var("PSADWATCHD_CHECK_INTERVAL ",
                char_psadwatchd_check_interval, index);
            find_char_var("PSADWATCHD_MAX_RETRIES ",
                char_psadwatchd_max_retries, index);
            find_char_var("PSADWATCHD_PID_FILE ", psadwatchd_pid_file, index);
        }
    }
    *psadwatchd_check_interval = atoi(char_psadwatchd_check_interval);
    *psadwatchd_max_retries    = atoi(char_psadwatchd_max_retries);
    fclose(config_ptr);
    return;
}
