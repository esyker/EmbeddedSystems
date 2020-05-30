#include <stdio.h>
#include <stdlib.h>
#include <cyg/kernel/kapi.h>
#include <cyg/io/io.h>
#include <ctype.h>
#include <cyg/error/codes.h>
#include <cyg/io/serialio.h>
#include <cyg/io/config_keys.h>
#include <cyg/infra/diag.h>

#define SOM 0xFD /* start of message */
#define EOM 0xFE /* end of message */
#define RCLK 0xC0 /* read clock */
#define SCLK 0XC1 /* set clock */
#define RTL 0XC2 /* read temperature and luminosity */
#define RPAR 0XC3 /* read parameters */
#define MMP 0XC4 /* modify monitoring period */
#define MTA 0XC5 /* modify time alarm */
#define RALA 0XC6 /* read alarms (temperature, luminosity, active/inactive) */
#define DATL 0XC7 /* define alarm temperature and luminosity */
#define AALA 0XC8 /* activate/deactivate alarms */
#define IREG 0XC9 /* information about registers (NREG, nr, iread, iwrite)*/
#define TRGC 0XCA /* transfer registers (curr. position)*/
#define TRGI 0XCB /* transfer registers (index) */
#define NMFL 0XCC /* notification memory (half) full */
#define CMD_OK 0 /* command successful */
#define CMD_ERROR 0xFF /* error in command */

#define CPT 0xD0 /*check period of transference*/
#define MPT 0xD1 /*modify period of transference */
#define CTTL 0xD2 /*check threshold temperature and luminosity for processing*/
#define DTTL 0xD3 /*define threshold temperature and luminosity for processing*/
#define PR 0xD4 /*process registers (max, min, mean) between instants t1 and t2 (h,m,s)*/
#define PTRC 0xD5 /*start periodic transfer*/
#define TRCACK 0xD6 /*acknowledgment of periodic transfer*/


#define TIMEOUT 50 /*timeout for receiving response from command in UI*/


#define TRUE 1
#define FALSE 0
#define NONE 255

#define MAX_MESSAGE 200 /*maximum size of message received (bytes)*/

#define NCOMMANDS  (sizeof(commands)/sizeof(struct command_d))
#define ARGVECSIZE 10 /*maximum size of argument*/
#define MAX_LINE   50 /*maximum size of command line*/
#define NT 4 /*number of threads*/
#define PRI 0 /*priority*/
#define STKSIZE 4096 /*thread stack size*/

void process_message(unsigned char* message, int size);
void receiveFromSerial(cyg_addrword_t data);
void cmd_sair (int argc, char **argv);
void cmd_ems (int argc, char **argv);
void cmd_emh (int argc, char **argv);
void cmd_rms (int argc, char **argv);
void cmd_rmh (int argc, char **argv);
void cmd_ini (int argc, char **argv);
void cmd_rc(int argc, char **argv);
void cmd_sc(int argc, char **argv);
void cmd_rtl(int argc, char **argv);
void cmd_sos (int argc, char **argv);
void cmd_rp (int argc, char **argv);
void cmd_mmp (int argc, char **argv);
void cmd_mta (int argc, char **argv);
void cmd_ra (int argc, char **argv);
void cmd_dtl (int argc, char **argv);
void cmd_aa (int argc, char **argv);
void cmd_ir (int argc, char **argv);
void cmd_trc (int argc, char **argv);
void cmd_tri (int argc, char **argv);
void writeToSerial (void);
void cmd_irl(int argc, char **argv);
void processingTask(void);
void alarm_func(cyg_handle_t alarmH, cyg_addrword_t data);
void cmd_pr(int argc, char **argv);
void cmd_dttl(int argc, char **argv);
void cmd_lr(int argc, char **argv);
void cmd_dr(int argc, char **argv);
void cmd_cpt(int argc, char **argv);
int my_getline (char** argv, int argvsize);
void monitor (void);
void cmd_mpt(int argc, char **argv);
void cmd_cttl(int argc, char **argv);
int bufflen(unsigned char * buff);


/*structure used to store the registers in a ring buffer*/
typedef struct register_
{
    unsigned char hours;
    unsigned char minutes;
    unsigned char seconds;
    unsigned char temperature;
    unsigned char luminosity;
} register_;


//list of commands available
struct 	command_d {
    void  (*cmd_fnct)(int, char**);
    char*	cmd_name;
    char*	cmd_help;
} const commands[] = {
    {cmd_sos,  "sos","                 help"},
    {cmd_sair, "sair","                sair"},
    {cmd_ini,  "ini","<d>              inicializar dispositivo (0/1) ser0/ser1"},
    {cmd_rc,   "rc","                  read clock"},
    {cmd_sc,   "sc","<h><m><s>         set clock"},
    {cmd_rtl,  "rtl","                 read temperature and luminosity"},
    {cmd_rp,   "rp","                  read parameters (PMON,TALA)"},
    {cmd_mmp,  "mmp","<p>              modify monitoring period (seconds - 0 deactivate)"},
    {cmd_mta,  "mta","<t>              mta t - modify time alarm (seconds)"},
    {cmd_ra,   "ra","                  read alarms (temperature, luminosity, active/inactive-1/0)"},
    {cmd_dtl,  "dtl","<t><l>           define alarm temperature and luminosity"},
    {cmd_aa,   "aa","<a>               activate/deactivate alarms (1/0)"},
    {cmd_ir,   "ir","                  information about registers (NREG, nr, iread, iwrite)"},
    {cmd_trc,  "trc","<n>              transfer n registers from current iread position"},
    {cmd_tri,  "tri","<n><i>           transfer n registers from index i (0 - oldest)"},
    {cmd_irl,  "irl","                 information about local registers (NRBUF, nr, iread, iwrite)"},
    {cmd_lr,   "lr","<n><i>            list n registers (local memory) from index i (0 - oldest)"},
    {cmd_dr,   "dr","                  delete registers (local memory)"},
    {cmd_cpt,  "cpt","                 check period of transference"},
    {cmd_mpt,  "mpt","<p>              modify period of transference (minutes - 0 deactivate)"},
    {cmd_cttl, "cttl","                check threshold temperature and luminosity for processing"},
    {cmd_dttl, "dttl","<t><l>          define threshold temperature and luminosity for processing"},
    {cmd_pr,   "pr","[<t1>[<t2>]]      transfer n registers from index i (0 - oldest)"}
};

const char TitleMsg[] = "\n Weather Station Control Monitor\n";
const char InvalMsg[] = "\nInvalid command!";

char stack[NT][STKSIZE]; //stack for each thread

cyg_handle_t threadsH[NT]; //thread handles
cyg_thread threads[NT]; //threads

cyg_mutex_t ring_buffer_mux;
cyg_mutex_t print_mux;

//mailbox for sending Task
cyg_handle_t mbx_sendingTaskH;
cyg_mbox mbx_sendingTask;
//mailbox for processing task
cyg_handle_t mbx_processingTaskH;
cyg_mbox mbx_processingTask;
//mailbox for UI task
cyg_handle_t mbx_UITaskH;
cyg_mbox mbx_UITask;

Cyg_ErrNo err;
cyg_io_handle_t serH; //device handler
int NRBUF = 100; //size of ring buffer
register_* RingBuffer;
int nr = 0; //number of registers with information
int iwrite = 0; //write index
int iread = 0; //read index
int num_unread_registers = 0; //number of not yet read registers (iwrite-iread)


int main(void)
{
    //create ring buffer
    RingBuffer = (register_*)malloc(sizeof(register_)*NRBUF);

    cyg_mutex_init(&ring_buffer_mux);
    cyg_mutex_init(&print_mux);

    //create mailboxes
    cyg_mbox_create( &mbx_sendingTaskH, &mbx_sendingTask);
    cyg_mbox_create( &mbx_processingTaskH, &mbx_processingTask);
    cyg_mbox_create( &mbx_UITaskH, &mbx_UITask);

    //create threads
    cyg_thread_create(PRI, (cyg_thread_entry_t*)processingTask, (cyg_addrword_t) 0,
    "ProcessingThread", (void *) stack[0], STKSIZE,
    &threadsH[0], &threads[0]);

    cyg_thread_create(PRI+1, (cyg_thread_entry_t*)writeToSerial, (cyg_addrword_t) 0,
    "WriteThread", (void *) stack[1], STKSIZE,
    &threadsH[1], &threads[1]);

    cyg_thread_create(PRI+2, (cyg_thread_entry_t*)receiveFromSerial, (cyg_addrword_t) 0,
    "ReceivingThread", (void *) stack[2], STKSIZE,
    &threadsH[2], &threads[2]);

    cyg_thread_create(PRI+3, (cyg_thread_entry_t*)monitor, (cyg_addrword_t) 0,
    "UIThread", (void *) stack[3], STKSIZE,
    &threadsH[3], &threads[3]);


    //initiate device
    cmd_ini(0, NULL);

    //start threads
    cyg_thread_resume(threadsH[0]);
    cyg_thread_resume(threadsH[1]);
    cyg_thread_resume(threadsH[2]);
    cyg_thread_resume(threadsH[3]);

    return 0;
}

/*-------------------------------------------------------------------------+
| Function: monitor        (executed in UI thread)
+--------------------------------------------------------------------------*/
void monitor (void)
{
    static char *argv[ARGVECSIZE+1], *p;
    int argc, i;

    cyg_mutex_lock(&print_mux);
    printf("%s Type sos for help\n", TitleMsg);
    cyg_mutex_unlock(&print_mux);
    for (;;) {
        cyg_mutex_lock(&print_mux);
        printf("\nCmd> ");
        cyg_mutex_unlock(&print_mux);
        /* Reading and parsing command line  ----------------------------------*/
        if ((argc = my_getline(argv, ARGVECSIZE)) > 0) {
            for (p=argv[0]; *p != '\0'; *p=tolower(*p), p++);
            for (i = 0; i < NCOMMANDS; i++)
            if (strcmp(argv[0], commands[i].cmd_name) == 0)
            break;
            /* Executing commands -----------------------------------------------*/
            if (i < NCOMMANDS)
            {
                unsigned char* m;

                //empty mailbox
                do {
                    m = cyg_mbox_tryget(mbx_UITaskH);
                } while(m != NULL);

                commands[i].cmd_fnct (argc, argv); //execute function corresponding to command typed

                m = cyg_mbox_timed_get(mbx_UITaskH, cyg_current_time()+TIMEOUT); //get response of command from mailbox with timeout

                if(m)
                {
                    process_message(m, bufflen(m)); //process message received
                    free(m);
                }

            }
            else
            {
                cyg_mutex_lock(&print_mux);
                printf("%s", InvalMsg);
                cyg_mutex_unlock(&print_mux);
            }
        } /* if my_getline */
    } /* forever */

}

/*-------------------------------------------------------------------------+
| Function: getline        (called from monitor)
+--------------------------------------------------------------------------*/
int my_getline (char** argv, int argvsize)
{
    static char line[MAX_LINE];
    char *p;
    int argc;

    fgets(line, MAX_LINE, stdin);

    /* Break command line into an o.s. like argument vector,
    i.e. compliant with the (int argc, char **argv) specification --------*/

    for (argc=0,p=line; (*line != '\0') && (argc < argvsize); p=NULL,argc++) {
        p = strtok(p, " \t\n");
        argv[argc] = p;
        if (p == NULL) return argc;
    }
    argv[argc] = p;
    return argc;
}


//process messages received as responses from UI commands
void process_message(unsigned char* message, int size)
{
    if(message[0] != SOM) //messages must start with SOM
    return;
    cyg_mutex_lock(&print_mux);
    switch(message[1]) //next comes the command code
    {
        case RCLK:
            if(message[2] == CMD_ERROR)
                printf("READ CLOCK: ERROR\n");
            else
                printf("READ CLOCK: Hours - %d, Minutes - %d, Seconds - %d\n", message[2], message[3], message[4]);
            break;
        case SCLK:
            if(message[2] == CMD_OK)
                printf("SET CLOCK: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("SET CLOCK: ERROR\n");
            break;
        case RTL:
            if(message[2] == CMD_ERROR)
                printf("READ TEMPERATURE AND LUMINOSITY: ERROR\n");
            else
                printf("READ TEMPERATURE AND LUMINOSITY: Temperature - %d, Luminosity - %d\n", message[2], message[3]);
            break;
        case RPAR:
            if(message[2] == CMD_ERROR)
                printf("READ PARAMETERS: ERROR\n");
            else
                printf("READ PARAMETERS: PMON - %d, TALA - %d\n", message[2], message[3]);
            break;
        case MMP:
            if(message[2] == CMD_OK)
                printf("MODIFY MONITORING PERIOD: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("MODIFY MONITORING PERIOD: ERROR\n");
            break;
        case MTA:
            if(message[2] == CMD_OK)
                printf("MODIFY TIME ALARM: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("MODIFY TIME ALARM: ERROR\n");
            break;
        case RALA:
            if(message[2] == CMD_ERROR)
                printf("READ ALARMS: ERROR\n");
            else
                printf("READ ALARMS: TEMPERATURE - %d, LUMINOSITY - %d, ACTIVE/INACTIVE - %d\n", message[2], message[3], message[4]);
            break;
        case DATL:
            if(message[2] == CMD_OK)
                printf("DEFINE ALARM TEMPERATURE AND LUMINOSITY: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("DEFINE ALARM TEMPERATURE AND LUMINOSITY: ERROR\n");
            break;
        case AALA:
            if(message[2] == CMD_OK)
                printf("ACTIVATE/DEACTIVATE ALARMS: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("ACTIVATE/DEACTIVATE ALARMS: ERROR\n");
            break;
        case IREG:
            if(message[2] == CMD_ERROR)
                printf("INFORMATION ABOUT REGISTERS: ERROR\n");
            else
            {
                printf("INFORMATION ABOUT REGISTERS: NREG - %d, nr - %d, iread - %d, iwrite - %d\n", message[2], message[3], message[4], message[5]);
            }
            break;
        case TRGC:
            if(message[2] == CMD_OK)
                printf("TRANSFERED REGISTERS FROM CURRENT IREAD POSITION: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("TRANSFERED REGISTERS FROM CURRENT IREAD POSITION: ERROR\n");
            break;
        case TRGI:
            if(message[2] == CMD_OK)
                printf("TRANSFERED REGISTERS FROM INDEX i: OK\n");
            else if(message[2] == CMD_ERROR)
                printf("TRANSFERED REGISTERS FROM INDEX i: ERROR\n");
            break;

        case CPT:
            if(message[2] == CMD_ERROR)
                printf("CHECK PERIOD OF TRANSFERENCE: ERROR\n");
            else if(message[2] == 0)
                printf("CHECK PERIOD OF TRANSFERENCE: DISABLED\n");
            else
                printf("CHECK PERIOD OF TRANSFERENCE: %d minutes\n", message[2]);
            break;
        case MPT:
            if(message[2] == CMD_ERROR)
                printf("MODIFY PERIOD OF TRANSFERENCE: ERROR\n");
            else if(message[2] == CMD_OK)
                printf("MODIFY PERIOD OF TRANSFERENCE: OK\n");
            break;
        case CTTL:
            if(message[2] == CMD_ERROR)
                printf("CHECK THRESHOLD TEMPERATURE AND LUMINOSITY FOR PROCESSING: ERROR\n");
            else
                printf("CHECK THRESHOLD TEMPERATURE - %d - AND LUMINOSITY - %d - FOR PROCESSING\n", message[2], message[3]);
            break;
        case DTTL:
            if(message[2] == CMD_ERROR)
                printf("DEFINE THRESHOLD TEMPERATURE AND LUMINOSITY FOR PROCESSING: ERROR\n");
            else if(message[2] == CMD_OK)
                printf("DEFINE THRESHOLD TEMPERATURE AND LUMINOSITY FOR PROCESSING: OK\n");
            break;
        case PR:
            if(message[2] == CMD_ERROR)
                printf("PROCESS REGISTERS BETWEEN INSTANTS T1 AND T2: ERROR\n");
            else
                printf("PROCESS REGISTERS BETWEEN INSTANTS T1 AND T2: TEMPERATURE - MAX %d - MIN %d - MEAN %d; LUMINOSITY - MAX %d - MIN %d - MEAN %d\n", message[2], message[3], message[4], message[5], message[6], message[7]);
            break;

    }
    cyg_mutex_unlock(&print_mux);
}

/*-------------------------------------------------------------------------+
| Function: cmd_sair - termina a aplicacao
+--------------------------------------------------------------------------*/
void cmd_sair (int argc, char **argv)
{
    exit(0);
}

/*-------------------------------------------------------------------------+
| Function: cmd_ini - inicializar dispositivo
+--------------------------------------------------------------------------*/
void cmd_ini(int argc, char **argv)
{
    cyg_mutex_lock(&print_mux);
    printf("io_lookup\n");
    cyg_mutex_unlock(&print_mux);
    if ((argc > 1) && (argv[1][0] = '1'))
    err = cyg_io_lookup("/dev/ser1", &serH);
    else err = cyg_io_lookup("/dev/ser0", &serH);
    cyg_mutex_lock(&print_mux);
    printf("lookup err=%x\n", err);
    cyg_mutex_unlock(&print_mux);
}

//send to communication task the command rc (read clock)
void cmd_rc(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=RCLK;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command sc (set clock)
void cmd_sc(int argc, char **argv)
{

    if (argc == 4) {
        unsigned char* buffer = (unsigned char*)malloc(6*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=SCLK;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=atoi(argv[3]);
        buffer[5]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command rc (read temperature and luminosity)
void cmd_rtl(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=RTL;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command rp (read parameters)
void cmd_rp(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=RPAR;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command mmp (modify monitoring period)
void cmd_mmp(int argc, char **argv)
{

    if (argc == 2) {
        unsigned char* buffer=(unsigned char*)malloc(4*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=MMP;
        buffer[2]=atoi(argv[1]);
        buffer[3]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command mta (modify time alarm)
void cmd_mta(int argc, char **argv)
{

    if (argc == 2) {
        unsigned char* buffer=(unsigned char*)malloc(4*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=MTA;
        buffer[2]=atoi(argv[1]);
        buffer[3]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command ra (read alarms)
void cmd_ra(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=RALA;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command dtl (define alarm temperature and luminosity)
void cmd_dtl(int argc, char **argv)
{

    if (argc == 3) {
        unsigned char* buffer=(unsigned char*)malloc(5*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=DATL;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command aa (activate/deactivate alarm)
void cmd_aa(int argc, char **argv)
{

    if (argc == 2) {
        unsigned char* buffer=(unsigned char*)malloc(4*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=AALA;
        buffer[2]=atoi(argv[1]);
        buffer[3]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command ir (information about registers)
void cmd_ir(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=IREG;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command trc (transfer n registers from current iread position)
void cmd_trc(int argc, char **argv)
{

    if (argc == 2) {
        unsigned char* buffer = (unsigned char*)malloc(4*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=TRGC;
        buffer[2]=atoi(argv[1]);
        buffer[3]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to communication task the command tri (transfer n registers from index i (0 - oldest))
void cmd_tri(int argc, char **argv)
{

    if (argc == 3) {
        unsigned char* buffer=(unsigned char*)malloc(5*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=TRGI;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=EOM;
        cyg_mbox_put(mbx_sendingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//execute the command irl (information about local registers)
void cmd_irl(int argc, char **argv)
{
    if (argc == 1) {
        cyg_mutex_lock(&ring_buffer_mux);
        cyg_mutex_lock(&print_mux);
        printf("INFORMATION ABOUT LOCAL REGISTERS: NRBUF - %d, nr - %d, iread - %d, iwrite - %d\n", NRBUF, nr, iread, iwrite);
        cyg_mutex_unlock(&print_mux);
        cyg_mutex_unlock(&ring_buffer_mux);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//execute the command lr (list n registers)
void cmd_lr(int argc, char **argv)
{
    int n;
    int num_reads = 0;
    int i;
    if (argc == 2) { //list n registers from index iread
        n = atoi(argv[1]); //number of registers is the first argument

        cyg_mutex_lock(&print_mux);
        cyg_mutex_lock(&ring_buffer_mux); //lock buffer
        while(n > 0 && num_unread_registers > 0 && num_reads < n)
        {
            //print registers starting at iread
            printf("\nREGISTER:\n");
            printf("HOURS: %d\n", RingBuffer[iread].hours);
            printf("MINUTES: %d\n", RingBuffer[iread].minutes);
            printf("SECONDS: %d\n", RingBuffer[iread].seconds);
            printf("TEMPERATURE: %d\n", RingBuffer[iread].temperature);
            printf("LUMINOSITY: %d\n", RingBuffer[iread].luminosity);
            num_reads++;
            iread++;
            if(iread >= NRBUF)
                iread = 0;
            num_unread_registers--;
        }
        cyg_mutex_unlock(&ring_buffer_mux); //unlock buffer
        printf("\nREAD %d REGISTERS FROM LOCAL BUFFER\n", num_reads);
        cyg_mutex_unlock(&print_mux);

    }
    else if(argc == 3) //list n registers from index i
    {
        n = atoi(argv[1]);
        i = atoi(argv[2]);
        cyg_mutex_lock(&ring_buffer_mux); //lock buffer
        //read index starts at iwrite-nr+i
        int aux_iread = (iwrite - nr);
        while(aux_iread < 0)
            aux_iread = aux_iread + NRBUF;

        aux_iread = (aux_iread + i);
        while(aux_iread >= NRBUF)
            aux_iread = aux_iread - NRBUF;

        cyg_mutex_lock(&print_mux);
        while(nr > 0 && n > 0 && num_reads < n)
        {
            printf("\nREGISTER:\n");
            printf("HOURS: %d\n", RingBuffer[aux_iread].hours);
            printf("MINUTES: %d\n", RingBuffer[aux_iread].minutes);
            printf("SECONDS: %d\n", RingBuffer[aux_iread].seconds);
            printf("TEMPERATURE: %d\n", RingBuffer[aux_iread].temperature);
            printf("LUMINOSITY: %d\n", RingBuffer[aux_iread].luminosity);
            num_reads++;

            if(aux_iread == iread) //if aux read index intersects iread, increment iread
            {
                iread++;
                num_unread_registers--;
                if(iread >= NRBUF)
                    iread = 0;
            }
            aux_iread++;
            if(aux_iread >= NRBUF)
                aux_iread = 0;

            if(aux_iread == iwrite)
                break;

        }
        cyg_mutex_unlock(&ring_buffer_mux); //unlock buffer

        printf("\nREAD %d REGISTERS FROM LOCAL BUFFER\n", num_reads);
        cyg_mutex_unlock(&print_mux);

    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

/*-------------------------------------------------------------------------+
| Function: cmd_sos - provides a rudimentary help
+--------------------------------------------------------------------------*/
void cmd_sos (int argc, char **argv)
{
    int i;

    cyg_mutex_lock(&print_mux);
    printf("%s\n", TitleMsg);
    for (i=0; i<NCOMMANDS; i++)
    printf("%s %s\n", commands[i].cmd_name, commands[i].cmd_help);
    cyg_mutex_unlock(&print_mux);
}

//executes command dr (delete local registers)
void cmd_dr(int argc, char **argv)
{
    if (argc == 1) {
        cyg_mutex_lock(&ring_buffer_mux);
        nr = 0;
        iwrite = 0;
        iread = 0;
        num_unread_registers = 0;
        cyg_mutex_unlock(&ring_buffer_mux);
        cyg_mutex_lock(&print_mux);
        printf("LOCAL REGISTERS DELETED\n");
        cyg_mutex_unlock(&print_mux);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to processing task the command cpt (check period of tranference)
void cmd_cpt(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=CPT;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to processing task the command mpt (modify period of transference)
void cmd_mpt(int argc, char **argv)
{

    if (argc == 2) {
        unsigned char* buffer=(unsigned char*)malloc(4*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=MPT;
        buffer[2]=atoi(argv[1]);
        buffer[3]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to processing task the command cttl (check threshold temperature and luminosity for processing)
void cmd_cttl(int argc, char **argv)
{

    if (argc == 1) {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=CTTL;
        buffer[2]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to processing task the command dttl (define threshold temperature and luminosity for processing)
void cmd_dttl(int argc, char **argv)
{

    if (argc == 3) {
        unsigned char* buffer=(unsigned char*)malloc(5*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=DTTL;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}

//send to processing task the command pr ( process registers (max, min, mean) between instants t1 and t2 (h,m,s))
void cmd_pr(int argc, char **argv)
{
    if (argc == 7) {
        unsigned char* buffer=(unsigned char*)malloc(9*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=PR;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=atoi(argv[3]);
        buffer[5]=atoi(argv[4]);
        buffer[6]=atoi(argv[5]);
        buffer[7]=atoi(argv[6]);
        buffer[8]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else if(argc == 4)
    {
        unsigned char* buffer=(unsigned char*)malloc(6*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=PR;
        buffer[2]=atoi(argv[1]);
        buffer[3]=atoi(argv[2]);
        buffer[4]=atoi(argv[3]);
        buffer[5]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else if(argc == 1)
    {
        unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
        buffer[0]=SOM;
        buffer[1]=PR;
        buffer[5]=EOM;
        cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
    }
    else {
        cyg_mutex_lock(&print_mux);
        printf("Wrong arguments");
        cyg_mutex_unlock(&print_mux);
    }
}


//copy "num_registers" from "registers" to the ring buffer
void copyToRingBuffer(unsigned char* registers, int num_registers)
{
    int j = 0;
    while(j < num_registers)
    {
        RingBuffer[iwrite].hours = registers[j*5];
        RingBuffer[iwrite].minutes = registers[j*5+1];
        RingBuffer[iwrite].seconds = registers[j*5+2];
        RingBuffer[iwrite].temperature = registers[j*5+3];
        RingBuffer[iwrite].luminosity = registers[j*5+4];

        iwrite++;
        if(iwrite >= NRBUF)
            iwrite = 0;

        if(num_unread_registers < NRBUF)
            num_unread_registers++; //num_unread_registers is at maximum NRBUF

        if(num_unread_registers == NRBUF)
        {
            iread = iwrite; //when the iwrite reaches iread, iread has to increment
        }

        if(nr < NRBUF)
            nr++; //nr is at maximum NRBUF

        j++;
    }
}

//pre-process message in receiving thread before sending it to the UI/processing thread
void pre_process_message(unsigned char* message_received, int index_message_received)
{
    //if it is a message of type transference, copy to ring buffer
    if(message_received[1] == TRGC || message_received[1] == TRGI || message_received[1] == TRCACK)
    {
        unsigned char* m_ = (unsigned char*)malloc(4*sizeof(unsigned char)); //message to send to UI/processing
        m_[0]=SOM;
        m_[1] = message_received[1];
        if(message_received[2] == CMD_ERROR)
        {
            m_[2] = CMD_ERROR;
        }
        else
        {
            int num_reg = (index_message_received + 1 - 3)/5; //number of registers received
            cyg_mutex_lock(&ring_buffer_mux); //lock buffer
            copyToRingBuffer(message_received+2, num_reg);
            cyg_mutex_unlock(&ring_buffer_mux); //unlock buffer
            m_[2] = CMD_OK;
        }
        m_[3] = EOM;
        if(message_received[1] == TRGC || message_received[1] == TRGI)
            cyg_mbox_put(mbx_UITaskH, m_); //TRGC and TRGI messages go to UI thread
        else if(message_received[1] == TRCACK)
            cyg_mbox_put(mbx_processingTaskH, m_); //TRCACK messages go to precessing task
        free(message_received);
    }
    else if(message_received[1] == NMFL)
    {
        cyg_mbox_put(mbx_processingTaskH, message_received);//NMFL message goes to processing task
    }
    else
        cyg_mbox_put(mbx_UITaskH, message_received); //all other messages go to UI task
}

//thread to receive messages from the device
void receiveFromSerial(cyg_addrword_t data)
{
    int receiving_message = FALSE; //tells wether we're currently receiving a message
    int index_message_received = 0; //current index of the received message
    unsigned char* message_received = NULL;
    unsigned char bit_received;
    int n = 1;
    while(1)
    {
        err = cyg_io_read(serH, &bit_received, (cyg_uint32 *)&n);
        if(err != ENOERR)
            continue;
        //printf("io_read err=%x, n=%d buf=%x\n", err, n, bit_received);
        if(bit_received == SOM) //message starts with SOM
        {
            message_received = (unsigned char*) malloc(MAX_MESSAGE*sizeof(unsigned char)); //start new message
            receiving_message = TRUE;
            index_message_received = 0;
            message_received[index_message_received] = SOM;
            index_message_received++;
        }
        else if(bit_received == EOM || index_message_received == MAX_MESSAGE-2) //message ends with EOM
        {
            receiving_message = FALSE;
            message_received[index_message_received] = EOM;
            pre_process_message(message_received, index_message_received); //pre-process and send the message to right thread
            index_message_received = 0;
        }
        else if(receiving_message)
        {
            message_received[index_message_received] = bit_received;
            index_message_received++;
        }

    }
}

//size of a message
int bufflen(unsigned char * buff)
{
    int count;
    for(count=0;buff[count]!=EOM;count++)
    {}
    return count+1;
}

//thread to write to device
void writeToSerial (void)
{
    while(1)
    {

        unsigned char* m;
        m=cyg_mbox_get(mbx_sendingTaskH); //get message from mailbox
        int n=bufflen(m);
        err=cyg_io_write(serH,m,(cyg_uint32 *)&n); //send to device
        /*int i = 0;
        for(i=0; i<n;i++)
        {
        printf("SENT: %x\n", m[i]);
        }*/
        free(m);
    }
}

//function associated with an alarm that periodically sends to the communication task the message PTRC (start periodic tranfer)
void alarm_func(cyg_handle_t alarmH, cyg_addrword_t data)
{
    unsigned char* buffer=(unsigned char*)malloc(3*sizeof(unsigned char));
    buffer[0]=SOM;
    buffer[1]=PTRC;
    buffer[2]=EOM;
    cyg_mbox_put(mbx_processingTaskH,(void*)buffer);
}

//determines if a given time (hour,minute,second) is between two other times (hourT1, minuteT1, secondT1) and (hourT2,minuteT2,secondT2).
//the value NONE can be used to express that a field does not exist
int isBetweenT1andT2(unsigned char hourT1, unsigned char minuteT1, unsigned char secondT1, unsigned char hourT2, unsigned char minuteT2, unsigned char secondT2, unsigned char hour, unsigned char minute, unsigned char second)
{
    if(hourT2 == NONE && minuteT2 == NONE && secondT2 == NONE && hourT1 == NONE && minuteT1 == NONE && secondT1 == NONE)
    {
        return TRUE;
    }
    if(hourT2 == NONE && minuteT2 == NONE && secondT2 == NONE)
    {
        //determine times in seconds to compare
        int time1 = 60*60*hourT1 + 60*minuteT1 + secondT1;
        int time_ = 60*60*hour + 60*minute + second;
        if(time_ >= time1)
        return TRUE;
        else
        return FALSE;
    }
    int time1 = 60*60*hourT1 + 60*minuteT1 + secondT1;
    int time2 = 60*60*hourT2 + 60*minuteT2 + secondT2;
    int time_ = 60*60*hour + 60*minute + second;

    if(time1<=time2 && time_>=time1 && time_<=time2)
        return TRUE;
    else if(time1>time2 && (time_>=time1 || time_<=time2))
        return TRUE;
    else return FALSE;
}

//updates the alarm according to the period specified. a period of 0 corresponds to turning off the alarm
void updateTransferAlarm(cyg_handle_t alarmH, int period_of_transference)
{
    if(period_of_transference != 0)
    {
        //printf("PERIOD OF TRANSFERENCE CHANGED TO: %d minutes\n", period_of_transference);
        cyg_alarm_initialize(alarmH, cyg_current_time() + period_of_transference*100*60, period_of_transference*100*60);
        cyg_alarm_enable(alarmH);
    }
    else
    {
        //printf("PERIOD OF TRANSFERENCE DISABLED\n");
        cyg_alarm_disable(alarmH);
    }
}

//function executed in the processing task
void processingTask(void)
{

    cyg_handle_t counterH, system_clockH, alarmH;
    cyg_tick_count_t ticks;
    cyg_alarm alarm;
    unsigned how_many_alarms = 0, prev_alarms = 0, tmp_how_many;
    system_clockH = cyg_real_time_clock();
    cyg_clock_to_counter(system_clockH, &counterH);
    cyg_alarm_create(counterH, alarm_func, //create alarm
    (cyg_addrword_t) &how_many_alarms, &alarmH, &alarm);


    int period_of_transference = 0;
    int threshold_temperature = 25;
    int threshold_lum = 2;
    int size;
    int num_reads;

    while(1)
    {
        unsigned char* m;
        m=cyg_mbox_get(mbx_processingTaskH);
        if(m[0] != SOM) //message must start with SOM
        continue;
        size=bufflen(m);
        unsigned char* m_;
        switch(m[1])
        {
            case CPT: //check period of tranference
                m_ = (unsigned char*)malloc(4*sizeof(unsigned char));
                m_[0] = SOM;
                m_[1] = CPT;
                m_[2] = period_of_transference;
                m_[3] = EOM;
                cyg_mbox_put(mbx_UITaskH, m_); //put message in UI mailbox
                break;
            case MPT: //modify period of transference
                m_ = (unsigned char*)malloc(4*sizeof(unsigned char));
                m_[0] = SOM;
                m_[1] = MPT;

                m_[3] = EOM;
                if(m[2] < 0)
                {
                    m_[2] = CMD_ERROR;
                }
                else
                {
                    m_[2] = CMD_OK;
                    //update alarm
                    period_of_transference = m[2];
                    updateTransferAlarm(alarmH, period_of_transference);
                }
                cyg_mbox_put(mbx_UITaskH, m_); //put message in UI mailbox
                break;
                case CTTL: //check threshold temperature and luminosity for processing
                m_ = (unsigned char*)malloc(5*sizeof(unsigned char));
                m_[0] = SOM;
                m_[1] = CTTL;
                m_[2] = threshold_temperature;
                m_[3] = threshold_lum;
                m_[4] = EOM;
                cyg_mbox_put(mbx_UITaskH, m_); //put message in UI mailbox
                break;

            case DTTL: //define threshold temperature and luminosity for processing
                //update thresholds
                threshold_temperature = m[2];
                threshold_lum = m[3];
                m_ = (unsigned char*)malloc(4*sizeof(unsigned char));
                m_[0] = SOM;
                m_[1] = DTTL;
                m_[2] = CMD_OK;
                m_[4] = EOM;
                cyg_mbox_put(mbx_UITaskH, m_); //put message in UI mailbox
                break;

            case PR: //process registers (max, min, mean) between instants t1 and t2 (h,m,s)
                num_reads = 0;
                float mean_temperature = 0.0;
                int min_temperature = 255;
                int max_temperature = 0;
                float mean_lum = 0.0;
                int min_lum = 255;
                int max_lum = 0;
                int hour1, minute1, second1, hour2, minute2, second2;
                //depending on the size of the message, determine fields that user inserted
                if(size == 9)
                {
                    hour1 = m[2];
                    minute1 = m[3];
                    second1 = m[4];
                    hour2 = m[5];
                    minute2 = m[6];
                    second2 = m[7];
                }
                else if(size == 6)
                {
                    hour1 = m[2];
                    minute1 = m[3];
                    second1 = m[4];
                    hour2 = NONE;
                    minute2 = NONE;
                    second2 = NONE;
                }
                else if(size == 3)
                {
                    hour1 = NONE;
                    minute1 = NONE;
                    second1 = NONE;
                    hour2 = NONE;
                    minute2 = NONE;
                    second2 = NONE;
                }
                else
                {
                    m_ = (unsigned char*)malloc(4*sizeof(unsigned char));
                    m_[0] = SOM;
                    m_[1] = PR;
                    m_[2] = CMD_ERROR;
                    m_[3] = EOM;
                    cyg_mbox_put(mbx_UITaskH, m_);
                    break;
                }

                cyg_mutex_lock(&ring_buffer_mux); //lock buffer
                //read all registers that are between T1 and T2
                int aux_iread = (iwrite - nr);
                while(aux_iread < 0)
                aux_iread = aux_iread + NRBUF;

                while(nr > 0)
                {
                    if(isBetweenT1andT2(hour1, minute1, second1, hour2, minute2, second2, RingBuffer[aux_iread].hours, RingBuffer[aux_iread].minutes, RingBuffer[aux_iread].seconds))
                    {
                        mean_temperature += RingBuffer[aux_iread].temperature;
                        if(RingBuffer[aux_iread].temperature < min_temperature) min_temperature = RingBuffer[aux_iread].temperature;
                        if(RingBuffer[aux_iread].temperature > max_temperature) max_temperature = RingBuffer[aux_iread].temperature;
                        mean_lum += RingBuffer[aux_iread].luminosity;
                        if(RingBuffer[aux_iread].luminosity < min_lum) min_lum = RingBuffer[aux_iread].luminosity;
                        if(RingBuffer[aux_iread].luminosity > max_lum) max_lum = RingBuffer[aux_iread].luminosity;

                        num_reads++;
                    }


                    aux_iread++;
                    if(aux_iread >= NRBUF)
                        aux_iread = 0;

                    if(aux_iread == iwrite)
                        break;

                }
                cyg_mutex_unlock(&ring_buffer_mux); //unlock buffer

                if(num_reads > 0)
                {
                    //determine mean, min and max
                    mean_temperature = (mean_temperature/num_reads);
                    mean_lum = (mean_lum/num_reads);
                    m_ = (unsigned char*)malloc(9*sizeof(unsigned char));
                    m_[0] = SOM;
                    m_[1] = PR;
                    m_[2] = max_temperature;
                    m_[3] = min_temperature;
                    m_[4] = (int)mean_temperature;
                    m_[5] = max_lum;
                    m_[6] = min_lum;
                    m_[7] = (int)mean_lum;
                    m_[8] = EOM;
                    cyg_mbox_put(mbx_UITaskH, m_);  //put message in UI mailbox
                }
                else
                {
                    m_ = (unsigned char*)malloc(4*sizeof(unsigned char));
                    m_[0] = SOM;
                    m_[1] = PR;
                    m_[2] = CMD_ERROR;
                    m_[3] = EOM;
                    cyg_mbox_put(mbx_UITaskH, m_);
                }
                break;

            case PTRC: //start periodic tranference
                cyg_mutex_lock(&print_mux);
                printf("STARTING PERIODIC TRANSFERENCE...\n");
                cyg_mutex_unlock(&print_mux);
                m_ = (unsigned char*)malloc(3*sizeof(unsigned char));
                m_[0] = SOM;
                m_[1] = PTRC;
                m_[2] = EOM;
                cyg_mbox_put(mbx_sendingTaskH, m_);  //put message in communication task mailbox
                break;

            case TRCACK: //tranference acknowledgment
                num_reads = 0;
                cyg_mutex_lock(&ring_buffer_mux); //lock buffer
                //list registers above threeshold
                cyg_mutex_lock(&print_mux);
                while(num_unread_registers > 0)
                {
                    if( RingBuffer[iread].temperature > threshold_temperature || RingBuffer[iread].luminosity > threshold_lum)
                    {
                        printf("\nREGISTER:\n");
                        printf("HOURS: %d\n", RingBuffer[iread].hours);
                        printf("MINUTES: %d\n", RingBuffer[iread].minutes);
                        printf("SECONDS: %d\n", RingBuffer[iread].seconds);
                        printf("TEMPERATURE: %d\n", RingBuffer[iread].temperature);
                        printf("LUMINOSITY: %d\n", RingBuffer[iread].luminosity);
                        num_reads++;
                    }

                    iread++;
                    num_unread_registers--;
                    if(iread >= NRBUF)
                    iread = 0;

                }
                cyg_mutex_unlock(&ring_buffer_mux); //unlock buffer
                printf("PERIODIC TRANFER COMPLETE. %d REGISTERS ABOVE THRESHOLD\n", num_reads);
                cyg_mutex_unlock(&print_mux);

                break;

            case NMFL: //notification of memory full
                cyg_mutex_lock(&print_mux);
                printf("NOTIFICATION OF MEMORY HALF FULL. PERIODIC TRANFER SET TO 1 MINUTE\n");
                cyg_mutex_unlock(&print_mux);
                //update alarm
                period_of_transference = 1;
                updateTransferAlarm(alarmH, period_of_transference);
                break;


        }
        free(m);
    }
}
