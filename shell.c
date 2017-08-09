/**
 * @file shell.c
 *
 * This module implements a simple shell interface via serial port. It display 'td>' prompt and receives input from user and process user commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "main.h"
#include "clock.h"
#include "mem.h"
#include "td_util.h"
#include "parser.h"
#include "shell.h"
#include "tracebuffer_param.h"
#include "tracebuffer.h"
#include "uutshell.h"
#include "version.h"
#include "terminal.h"
#include "interface_uart.h"
#include "system.h"
#include "executor.h"
#include "collection.h"
#include "protocol_td.h"
#include "protocol_serial.h"

/**
 * Private data members specific to this module. Contains following members:
 *
 * - sys Poiner to _system_t
 * - fileBuffer buffer to sore test case file
 * - td_reboot It is set to 1 when user choose to exit td> reboot
 */
typedef struct _shell_private_data_t shell_private_data_t;
struct _shell_private_data_t
{
    system_t *sys;

    // Test Data File
    char *fileBuffer;
    bool L2P;
    bool td_reboot;
    int mini_txt_timeout;
    int debugNoTimestampOnTraces;
    int debugPrintHardwareLines;
};

static int shell_exit = 0;


/*--- Function Declarations -------*/
static void Usage_Print(shell_t *self, char* buff);
static int ReceiveBinaryDataFromPC(shell_t *self, unsigned char * ptr, int length);
static int GetANumberFromUser(shell_t *self, char *promptMessage);
static void HexDump_Print(shell_t *self, int fd, unsigned long addr, size_t len, unsigned long offset, int width, unsigned int stride);
static int ReceiveFileFromPC(shell_t *self, bool testcaseFile, unsigned int length);

//static int Ishelp(shell_t *self, char *buff);

// Forward Declarations
static void Reboot(shell_t *self, char* buff);
static void HardResetUUT(shell_t *self, char* buff);
static void MasterResetUUT(shell_t *self, char* buff);
static void Status(shell_t *self, char* buff);
static void DpramNop(shell_t *self, char* buff);
static void DpramMemoryTest(shell_t *self, char* buff);
static void ExecuteTests(shell_t *self, char* buff);
static int ReceiveFileFromPC(shell_t *self, bool testcaseFile,unsigned int length);
static int ReceiveBinaryDataFromPC(shell_t *self, unsigned char * ptr,int length);
static int GetANumberFromUser(shell_t *self, char *promptMessage);
static void MemoryDump(shell_t *self, char * buff);
static void MemoryWrite(shell_t *self, char* buff);
static void MemoryRead(shell_t *self, char* buff);
static void HexDump_Print(shell_t *self, int fd, unsigned long addr, size_t len, unsigned long offset, int width, unsigned int stride);
static int SearchCommand(shell_t *self, char *str);
static int Shell_ProcessCommand(shell_t *self, char *buff);
static void Shell_PrintPrompt(shell_t *self);
static void SetTDDebugMode(shell_t *self, char* buff);
static void SetTDTimeStampsOnTraces(shell_t *self, char* buff);
static void SetTDDebugPrintHardwareLines(shell_t *self, char* buff);
static void TransferTestDataFile(shell_t *self, char* buff);
static void TransferFirmwareFile(shell_t *self, char* buff);
static void DumpUUTSerialBuffer(shell_t *self, char* buff);
static void Version(shell_t *self, char* buff);
static void Comment(shell_t *self, char* buff);
static void Exit(shell_t *self, char* buff);
static void Linux(shell_t *self, char* buff);

typedef void (*CommandFunction)(shell_t *, char*);

/**
 * Each command has following data
 *
 * - cmd Name of command
 * - desc Command description which appears on Test driver terminal
 * - fp Function pointer to each function to be executed for each command
 */
struct ACmd
{
    char *cmd;
    char *desc;
    CommandFunction fp;
};

static struct ACmd cmdTable[] =
{
        {.cmd="upload testdata",         .desc="Upload testcase data file to Test Driver", .fp=TransferTestDataFile},
        {.cmd="upload firmware",         .desc="Upload firmware file to Test Driver", .fp=TransferFirmwareFile},
        {.cmd="td debugmode",            .desc="Set Test Driver debug mode ON/OFF", .fp=SetTDDebugMode},
        {.cmd="td timestamps",           .desc="Turn ON/OFF time stamp on traces", .fp=SetTDTimeStampsOnTraces},
        {.cmd="td printhardwarelines",   .desc="Prints Hardware Lines Status (Ready, Start, Done etc)", .fp=SetTDDebugPrintHardwareLines},
        {.cmd="md",                      .desc="Dump Memory contents", .fp=MemoryDump},
        {.cmd="mr",                      .desc="Read from memory", .fp=MemoryRead},
        {.cmd="mw",                      .desc="Write data into memory", .fp=MemoryWrite},
        {.cmd="dpram test",              .desc="DPRAM Memory Test", .fp=DpramMemoryTest},
        {.cmd="dpram nop",               .desc="Execute DPRAM NOP", .fp=DpramNop},
        {.cmd="reboot",                  .desc="Reboot Test Driver", .fp=Reboot},
        {.cmd="status",                  .desc="Display status of UUT", .fp=Status},
        {.cmd="help",                    .desc="Print Usage", .fp=Usage_Print},
        {.cmd="run",                     .desc="Execute Tests", .fp=ExecuteTests},
        {.cmd="uut dump",                .desc="Dump UUT Serial Buffer", .fp=DumpUUTSerialBuffer},
        {.cmd="uut masterreset",         .desc="Master Reset UUT via GPIO", .fp=MasterResetUUT},
        {.cmd="uut hardreset",           .desc="Hard Reset UUT via GPIO", .fp=HardResetUUT},
        {.cmd="exit",                    .desc="Exit Test Driver", .fp=Exit},
        {.cmd="linux",                   .desc="Start Linux System Shell", .fp=Linux},
        {.cmd="uut",                     .desc="Start UUT Serial Subshell", .fp=UUTShell_MainLoop},
        {.cmd="ver",                     .desc="Print version info", .fp=Version},
        {.cmd="#",                       .desc="Comment line", .fp=Comment},
        {.cmd="?",                       .desc="Print Usage\n", .fp=Usage_Print},
};

/** @brief Exit test driver mode
 *
 * This function exits test driver
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Exit(shell_t *self, char* buff)
{
    /* exit only if this is not the first process */
    if(getpid() != 1 && getppid() != 1)
       td_exit = TRUE;
}

/** @brief Start the linux system shell
 *
 * This function starts the linux system shell
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 13-Sep-2013
 */
static void Linux(shell_t *self, char* buff)
{
    int pid, status = 0;
    char *args[] = {"sh", NULL};

    pid = fork();
    if(0 == pid)
    {
        /* in child process */
        execv(SYSTEM_SHELL, args);
    }
    else
    {
        /* in parent wait for system shell to exit */
        wait(&status);
    }
}

/** @brief Prints usage each command
 *
 * This function displays functionality of each command
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Usage_Print(shell_t *self, char* buff)
{
    int i = 0;
    int length = sizeof(cmdTable) / sizeof(struct ACmd);

    printf("\n");
    printf("Gen 3.0 Test System (c) 2013 Finisar Australia\n\n");
    Version(self, buff);
    printf("Available Commands:\n");

    for (i = 0; i < length; i++)
    printf("%-22s %s\n", cmdTable[i].cmd, cmdTable[i].desc);
    printf("\n");
}

/** @brief Prints test driver version
 *
 * This function displays test driver version
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Version(shell_t *self, char* buff)
{
    printf("Test Driver v%02d%s revision %s\n", VERSION_MAJOR, VERSION_PATCH, ver_rev());
    printf("Source URL: %s\n", ver_url());
    printf("Build Machine: %s\n", ver_comp());
    printf("Build Date: %s\n", ver_date());
}

static void Comment(shell_t *self, char* buff)
{
    /*Comment line*/
}

/** @brief Prints test driver prompt
 *
 * This function displays "td>"
 *
 * @param self pointer to self
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Shell_PrintPrompt(shell_t *self)
{
    printf("td>");
}

/** @brief Set test driver debug mode
 *
 * This function sets test driver debug mode
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void SetTDDebugMode(shell_t *self, char* buff)
{
    int i;
    int debugMode;

    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    i = GetANumberFromUser(self, "Test Driver DEBUG Mode (1-ON 0-OFF) : ");

    debugMode = (i ? DEBUG_MODE_ON : DEBUG_MODE_OFF);

    shell_data->sys->set_debug_mode(shell_data->sys, debugMode);
    td_dbg=debugMode;
    printf("DEBUG Mode = %d\n", debugMode);
}

/** @brief Turn on/off time stamps
 *
 * This function turn on/off time stamps for trace
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void SetTDTimeStampsOnTraces(shell_t *self, char* buff)
{
    int i;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    i = GetANumberFromUser(self, "Tracer - Turn OFF time stamps? (1-YES 0-NO) : ");
    //Normalize the input number, treat any nonzero value 1
    shell_data->debugNoTimestampOnTraces = (i ? 1 : 0);
    if(shell_data->debugNoTimestampOnTraces)
        Tracer_EnableNoTimeStampOnTraces();
    else
        Tracer_DisableNoTimeStampOnTraces();
    printf("NoTimestampOnTraces = %d\n", shell_data->debugNoTimestampOnTraces);

}

/** @brief Turn on/off hardware lines print
 *
 * This function turn on/off hardware lines print
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void SetTDDebugPrintHardwareLines(shell_t *self, char* buff)
{
    int i;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    i = GetANumberFromUser(self, "Debug - Print Hardware Lines Status? (1-YES 0-NO) : ");

    //Normalize the input number, treat any nonzero value 1
    shell_data->debugPrintHardwareLines = (i ? 1 : 0);
    shell_data->sys->set_print_hardware_lines(shell_data->sys, shell_data->debugPrintHardwareLines);
    printf("Debug - Print Hardware Lines Status = %d\n", shell_data->debugPrintHardwareLines);

}

/** @brief Transfer test case file
 *
 * This function transfer test case file from PC to test driver
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void TransferTestDataFile(shell_t *self, char* buff)
{
    long tempLength;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    shell_data->L2P = FALSE;

    tempLength = GetANumberFromUser(self, "Enter TestDataFile Length: ");
    if (tempLength > 0)
      {
        printf("Transfer Test Data File in Binary (%ld bytes)\n", tempLength);
        fflush(stdout);
        if (ReceiveFileFromPC(self, TRUE, tempLength) == 0)
        {
            /* no error */
        }
    }
}

/** @brief Transfer firmware file
 *
 * This function transfer test case file from PC to test driver
 *
 * @param self pointer to self
 * @param Input data entered by user
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void TransferFirmwareFile(shell_t *self, char* buff)
{
    long tempLength;

    /** Check if we have a free slot to download firmware? */
    shell_private_data_t *shell_private = (shell_private_data_t *) self->_private;
    system_t *system = shell_private->sys;
    collection_t *firmwares = system->get_firmwares(system);
    int firmwaresArrayLength = firmwares->size(firmwares);
    shell_private->L2P = FALSE;

    if ((firmwaresArrayLength) >= FIRMWARE_MAX_COUNT)
    {
        printf("Already %d firmware files exist.\n", FIRMWARE_MAX_COUNT);
    } else
    {
        char msg[80];

        Tracer_vnsnprintf(msg, sizeof(msg), "Enter Firmware %d File Length: ", firmwaresArrayLength + 1);
        tempLength = GetANumberFromUser(self, msg);
        if (tempLength)
        {
            printf("Transfer Firmware %d in Binary (%ld bytes)\n",
            firmwaresArrayLength + 1, tempLength);
            if (ReceiveFileFromPC(self, FALSE, tempLength) == 0)
            {
                /* no error */
            }
        }
    }
}

/** @brief Add header to fileBuffer
 *
 * This function add necessary header to fileBuffer so parser can interpret
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static int add_headers(shell_t *self, char* buff)
{
    char begin[100], end[100], ex[100], *result;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    snprintf(begin, sizeof(begin), "T>BEGIN %d\n", SHELL_MINI_TXT_TC_ID);
    snprintf(end, sizeof(end), "T>END %d\n", SHELL_MINI_TXT_TC_ID);
    snprintf(ex, sizeof(ex), "T>EX %d %d %d %d %d\n", SHELL_MINI_TXT_TC_ID, SHELL_MINI_TXT_REPEAT_COUNT, SHELL_MINI_TXT_STOP_ON_FAILURE, shell_data->mini_txt_timeout, SHELL_MINI_TXT_FLUSH_ON_END);

    result = (char*) td_malloc(strlen(buff) + strlen(begin) + strlen(end) + strlen(ex) + 1);

    if(!result)
    {
        printf("Error: failed to allocate memory\n");
        return 1;
    }
    else
    {
        result[0] = '\0';
        strcat((char*) result, begin);
        strcat((char*) result, buff);
        strcat(result, (char*) end);
        strcat(result, (char*) ex);
        shell_data->fileBuffer = result;
        return 0;
    }
}

/** @brief Prints buffers of uut to test driver
 *
 * This function prints buffers of uut serial port to test driver
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void DumpUUTSerialBuffer(shell_t *self, char* buff)
{
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    uart_t *uartUUT = shell_data->sys->get_uart_uut(shell_data->sys);

    S_internal_DumpUUTSerial(uartUUT);
}

/** @brief Reads memory from Test Driver in specified block size
 *
 * This function reads memory from Test Driver in specified block size
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void MemoryDump(shell_t *self, char* buff)
{
    int count, width;
    long array[100];
    const int maxBytesCount = 800;
    const int expectedCount = 4;
    unsigned long addr;

    if(Parser_GetNumbersFromString(buff, array, sizeof(array)/sizeof(long))!=expectedCount)
        printf("Error: Number of array elements returned didn't match\n");
    else
    {
        printf("%d\n", Parser_GetNumbersFromString(buff, array, sizeof(array)/sizeof(long)));
        addr = array[1];
        count = array[2];

        if (count > maxBytesCount)
        {
            count = maxBytesCount;
            printf("\nMax dump size = %d", count);
        }
        width = array[3];
        switch (width)
        {
        case 0:
            width = 1;
            /* fall-through */
        case 1:
        case 2:
        case 4:
            if (addr & (width - 1))
            {
                addr &= ~(width - 1);
                printf("\nAligning offset for %d-byte access = 0x%08lx", width,
                        addr);
            }
            break;

        default:
            printf("\nInvalid width: %d\n", width);
            return;
        }
        printf("\n");
        HexDump_Print(self, STDOUT_FILENO, addr, count, addr, width, 1);
        }
}

/** @brief Test DPRAM memory
 *
 * This function does DPRAM memory test by writing and reading the values of the address
 * If user enters 1, starts the test with default setting
 * If user enters 0xFEEDFEED, starts the test with user modified settings
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void DpramMemoryTest(shell_t *self, char* buff)
{
    char* tst_str = "D>TST\n";
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
    {
        td_free(shell_data->fileBuffer);
        shell_data->fileBuffer=NULL;
    }

    shell_data->fileBuffer = tst_str;
    shell_data->L2P=TRUE;
    ExecuteTests(self,tst_str);
}

/** @brief Read memory from Test Driver
 *
 * This function reads memory from Test Driver
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void MemoryRead(shell_t *self, char* buff)
{
    volatile intptr_t p;
    long array[100];
    const int expectedCount = 2;

    if(Parser_GetNumbersFromString(buff, array, sizeof(array)/sizeof(long))!=expectedCount)
           printf("Error: Number of array elements returned didn't match\n");
    else
    {
        p = array[1];
        printf("0x%08X\n", *((volatile unsigned int *) p));
    }
}

/** @brief Write to Test Driver memory
 *
 * This function writes to Test Driver memory
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void MemoryWrite(shell_t *self, char* buff)
{
        volatile intptr_t p;
        int data;
        long array[100];
        const int expectedCount = 3;

        if(Parser_GetNumbersFromString(buff, array, sizeof(array)/sizeof(long))!=expectedCount)
             printf("Error: Number of array elements returned didn't match\n");
        else
        {
            p = array[1];
            data = array[2];
            *((volatile unsigned int *) p) = data;
        }
}

/** @brief Reboot Test driver
 *
 * This function reboots Test driver
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Reboot(shell_t *self, char* buff)
{
    shell_private_data_t *shell_private = (shell_private_data_t *) self->_private;
    shell_private->td_reboot = TRUE;
}

/** @brief UUT Master reset
 *
 * This function does UUT master reset
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void MasterResetUUT(shell_t *self, char* buff)
{
    char* mr_str = "G>MR\n";
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
    {
        td_free(shell_data->fileBuffer);
        shell_data->fileBuffer=NULL;
    }

    shell_data->fileBuffer = mr_str;
    shell_data->L2P=TRUE;
    ExecuteTests(self, mr_str);
}

/** @brief UUT Hard reset
 *
 * This function does UUT hard reset
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void HardResetUUT(shell_t *self, char* buff)
{
    char* hr_str = "G>HR\n";
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
    {
        td_free(shell_data->fileBuffer);
        shell_data->fileBuffer=NULL;
    }

    shell_data->fileBuffer = hr_str;
    shell_data->L2P=TRUE;
    ExecuteTests(self, hr_str);
}

/** @brief Send NOP command
 *
 * This function send NOP command
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void DpramNop(shell_t *self, char* buff)
{
    char* nop_str = "D>WM 0x20 1\nD>WM 0x21 0x1\nG>ST 0 0\n";
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
    {
        td_free(shell_data->fileBuffer);
        shell_data->fileBuffer=NULL;
    }

    shell_data->fileBuffer = nop_str;
    shell_data->L2P=TRUE;
    ExecuteTests(self,nop_str);
}

/** @brief shows status
 *
 * This function shows status of hardware Lines and status, hardware, error code registers
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void Status(shell_t *self, char* buff)
{
    char *result, *lines, *status_reg, *err_reg, *hw_reg;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    lines = "G<PS 1\nG<RD 1\nG<DN 1\nG<ER 0\nG<AL 1\n";
    status_reg = "D<RM 0x23 1\n";
    err_reg = "D<RM 0x25 1\n";
    hw_reg = "D<RM 0x29 1\n";
    result = (char*) td_malloc(strlen(lines) + strlen(status_reg) + strlen(err_reg) + strlen(hw_reg) + 1);

    if(!result)
    {
        printf("Error: failed to allocate memory\n");
    }
    else
    {
    result[0] = '\0';

    strcat(result, lines);
    strcat(result, status_reg);
    strcat(result, err_reg);
    strcat(result, hw_reg);

    if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
    {
        td_free(shell_data->fileBuffer);
        shell_data->fileBuffer=NULL;
    }

    shell_data->fileBuffer = result;
    shell_data->L2P = TRUE;
    ExecuteTests(self, result);
    td_free(result);
    result=NULL;
    }
}


/** @brief Send file buffer to parser
 *
 * This function initializes Parser, Executor, and Tracer then sends file buffer to parser
 *
 * @param self pointer to self
 * @param user input buffer
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void ExecuteTests(shell_t *self, char *buff)
{
    int error =0;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    system_t *system = shell_data->sys;

    tracer_params_t tracer_params;
    XTime currentTime;


    if (!shell_data->fileBuffer)
    {
        printf("No test case data file. First transfer test case data file\n");
    }
    else
    {
        if(shell_data->L2P == TRUE)
        {
            error = add_headers(self, buff);
        }

        if(error == 0)
        {
            clk_gettime(&currentTime);
            //printf("Starting Execution...\n");

            // NOTE: all three parser/executor/tracer must be initialized
            error = Parser_Init();
            if (error == 0)
            {
                error = Executor_Init();
                if (error ==0)
                {
                    tracer_params.autoflush = TRUE;
                    tracer_params.size = TRACE_BUFFER_DEFAULT_SIZE;
                    tracer_params.starttime = currentTime;
                    tracer_params.system= system;
                    tracer_params.noTimestampOnTraces = shell_data->debugNoTimestampOnTraces;
                    tracer_params.manualorauto = (shell_data->L2P == TRUE) ? 1 : 0;
                    error = Tracer_Init(&tracer_params);
                    if (error == 0)
                    {
                        if(shell_data->L2P == FALSE)
                        {
                            Tracer_printf(FROM_TD,"TestDriver_Init - Test Driver v%02d%s revision %s\n", VERSION_MAJOR, VERSION_PATCH, ver_rev());
                            Tracer_printf(FROM_TD, "Source URL: %s\n", ver_url());
                            Tracer_printf(FROM_TD, "Build Machine: %s\n", ver_comp());
                            Tracer_printf(FROM_TD, "Build Date: %s\n", ver_date());
                        }

                        error = Parser_parse(shell_data->fileBuffer, system);
                        shell_data->fileBuffer=NULL; //Parser_parse() will free this pointer as soon as it is not needed

#if TD_STANDALONE_MODE
                        error += Tracer_Realloc(&tracer_params);
#endif

                        if (error == 0)
                        {
                            error = Executor_Run(system);
                            if (error == 0)
                            {
                                Tracer_FlushTraceBuffer();
                                fflush(stdout);
                            }
                            else
                            {
                                printf("Executor Run failed! FATAL ERROR!!!\n");
                                Tracer_FlushTraceBuffer();
                                fflush(stdout);
                            }
                        }
                        else
                        {
                            if(shell_data->L2P == FALSE)
                            {
                                printf("Parsing failed! FATAL ERROR!!!\n");
                                Tracer_FlushTraceBuffer();
                                fflush(stdout);
                            }
                            else
                                printf("Bad command\n"); // same message as Gen 2.0
                        }
                    }
                    else
                    {
                        printf("Tracer_init failed! FATAL ERROR!!!\n");
                    }
                }
                else
                {
                    printf("Executor_Init failed! FATAL ERROR!!!\n");
                    Tracer_FlushTraceBuffer();
                    fflush(stdout);
                }
            }
            else
            {
                printf("Parser_Init failed! FATAL ERROR!!!\n");
                Tracer_FlushTraceBuffer();
                fflush(stdout);
            }
        }

        Tracer_Destroy();
        Executor_Destroy();
        Parser_Destroy();

        if(shell_data->L2P == FALSE)
        {
            printf("Test Execution Finished\n*\n*\n*\n*\n");
            fflush(stdout);
        }
    }

    shell_data->td_reboot = TRUE;
}

/** @brief Send file buffer to parser
 *
 * This function initializes Parser, Executor, and Tracer then sends File buffer to parser
 *
 * @param self pointer to self
 * @param 1 if the file is test case file 0 if it is firmware file
 * @param Length of file
 * @return 0 on success 1 on failure
 * @author Yuki
 * @date 14-June-2013
 */
static int ReceiveFileFromPC(shell_t *self, bool testcaseFile, unsigned int length)
{
    unsigned char *fPtr = NULL;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    system_t *system = shell_data->sys;

    if (testcaseFile == TRUE)
    {
        /** Allocate memory for the Test Case File */
        fPtr = td_malloc(length + 1);
        if (!fPtr)
        {
            printf("ReceiveFileFromPC: Failed to allocate memory size = %u, %s:%d\n", length + 1, __FILE__, __LINE__);
            return 1;
        }

        if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
        {
            td_free(shell_data->fileBuffer);
            shell_data->fileBuffer=NULL;
        }

        shell_data->fileBuffer = (char *) fPtr;
    } else
    {
        /** Check if we have a free slot to download firmware? */

        collection_t *firmwares = system->get_firmwares(system);
        if ((firmwares->size(firmwares)) >= FIRMWARE_MAX_COUNT)

        {
            printf("ERROR Downloading Firmware: Limit reached. FIRMWARE_MAX_COUNT = %d\n", FIRMWARE_MAX_COUNT);
            return 1;
        }

        /** Allocate memory for Firmware and Download the Firmware into next empty slot */
        fPtr = td_malloc(length);
        if (!fPtr)
        {
            printf("ReceiveFileFromPC: Failed to allocate memory size = %u, %s:%d\n",length, __FILE__, __LINE__);
            return 1;
        }

        struct Firmware *firmware = (struct Firmware *) td_malloc(sizeof(struct Firmware));
        if (!firmware)
        {
            printf("ReceiveFileFromPC: Failed to allocate memory for struct Firmware = %s:%d\n",__FILE__, __LINE__);
            td_free(fPtr);
            fPtr=NULL;
            return 1;
        }

        firmware->ptr = fPtr;
        firmware->length = length;
        firmwares->add(firmwares, firmware);
        printf("Downloading Firmware into Firmware Buffer %d, Remaining Firmware Buffers = %d\n",firmwares->size(firmwares) - 1,FIRMWARE_MAX_COUNT - firmwares->size(firmwares));

    }

    if (ReceiveBinaryDataFromPC(self, fPtr, length))
    {
        printf("fatal error: Download failed\n");
        td_free(fPtr);
        fPtr=NULL;
        return 1;
    }

    if (testcaseFile == TRUE)
    {
        fPtr[length] = '\0';
    }

    printf("File Loaded successfully over RS232\n");

    return 0;
}
/** @brief Receive binary data from PC
 *
 * This function receives binary data from PC
 *
 * @param self pointer to self
 * @param Pointer to file
 * @param Length of file
 * @return 0
 * @author Yuki
 * @date 14-June-2013
 */
static int ReceiveBinaryDataFromPC(shell_t *self, unsigned char * ptr,int length)
{
    tty_buf tbuf;
    int offset;
    ssize_t bytes_read;

    tty_raw(STDIN_FILENO, &tbuf); /* put terminal in raw mode */
    offset = 0;
    while (offset < length)
    {
        bytes_read = read(STDIN_FILENO, ptr + offset, length - offset);
        if (bytes_read > 0)
        {
            offset += bytes_read;
        }
    }
    tty_reset(&tbuf); /* restore back terminal settings */

    return 0;
}

/** @brief Receive binary data from PC
 *
 * This function gets a line of user input then retrieves numbers from it
 *
 * @param self pointer to self
 * @param Any prompt message for the user input
 * @return Number read from user input
 * @author Yuki
 * @date 14-June-2013
 */
static int GetANumberFromUser(shell_t *self, char *promptMessage) {
    long num = 0;
    char *buff;
    size_t bytes_read, nbytes;

    printf("%s", promptMessage);
    fflush(stdout);

    buff = NULL;
    bytes_read = getline(&buff, &nbytes, stdin);
    if (td_dbg==DEBUG_MODE_ON)
        printf("[getline_malloc] ptr=%p\t size=0x%zx\t \n", buff, nbytes);

    if (bytes_read > 0)
        Parser_GetNumbersFromString(buff, &num, 1);
    if (buff)
    {
        td_free(buff);
        buff=NULL;
    }

    return num;
}


/** @brief Prints data of requested address of Test Driver in hex format
 *
 * This function prints data of requested address of Test Driver in hex format
 *
 * @param self pointer to self
 * @param File descriptor
 * @param Adress to read from
 * @param Length of data to read
 * @param Offset to read at
 * @param Width
 * @param stride
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void HexDump_Print(shell_t *self, int fd, unsigned long addr, size_t len, unsigned long offset, int width, unsigned int stride) {
    char *p, *q;
    int i, j, n;
    unsigned int c, s, v, o;
    char buf[128];

    n = (width < 4) ? 1 << (5 - width) : 4;
    o = n * (width * 2 + 1); /* aaaaaaaa: xx xx xx xx xx xx xx xx */
    memset(buf, ' ', sizeof(buf) - 1); /* fill with spaces */
    buf[sizeof(buf) - 1] = '\0'; /* ensure it is null-terminated */
    p = q = buf;
    for (i = 0; i < (len / width); i += stride) { /* length is in bytes */
        if (((i / stride) & (n - 1)) == 0) {
            if (i) {
                *(q++) = '\n';
                *q = '\0';
                fwrite(buf, 1, q - buf, stdout);
            }
            snprintf(buf, sizeof(buf), "%08lx:", offset + i); /* "width" addresses */
            p = buf + 9;
            q = buf + 9 + o;
            *(q++) = ' ';
            *(q++) = ' ';
        }
        *(p++) = ' ';
        switch (width) {
        case 1:
            v = *((uint8_t*) addr + i) & 0xff;
            snprintf(p, (buf + sizeof(buf)) - p, "%02X", v);
            p += 2;
            *p = ' ';
            break;

        case 2:
            v = *((uint16_t*) addr + i) & 0xffff;
            snprintf(p, (buf + sizeof(buf)) - p, "%04X", v);
            p += 4;
            *p = ' ';
            break;

        case 4:
        default:
            v = *((uint32_t*) addr + i);
            snprintf(p, (buf + sizeof(buf)) - p, "%08X", v);
            p += 8;
            *p = ' ';
            break;
        }
        for (j = 0; j < width; j++) {
            s = (width - j - 1) << 3;
            c = (v >> s) & 0xff;
            *(q++) = util_isgraph(c) ? c : '.';
        }

    }

    if (p != (buf + 9)) { /* finish it off */
        *(q++) = '\n';
        *q = '\0';
        memset(p, ' ', (buf + 9 + o) - p);
        fwrite(buf, 1, q - buf, stdout);
    }
}

/** @brief Searches command from user input
 *
 * This function compares the iuser input with the command list
 *
 * @param self pointer to self
 * @param User input buffer
 * @return -1 if there is no match
 * @author Yuki
 * @date 14-June-2013
 */
static int SearchCommand(shell_t *self, char *str) {
    int i = 0;
    int length = sizeof(cmdTable) / sizeof(struct ACmd);

    for (i = 0; i < length; i++)
        if (!strncmp(str, cmdTable[i].cmd, strlen(cmdTable[i].cmd)))
            return i;
    return -1;
}

/** @brief Checks if user seeks help information for the command
 *
 * This function checks if user is seeks help information for the command
 *
 * @param self pointer to self
 * @param User input buffer
 * @return 0 if there is no match 1 if there is a match
 * @author Yuki
 * @date 14-June-2013
 */
/*static int Ishelp(shell_t *self, char *buff) {
    if (Parser_WildCardMatch(buff, "--help"))
    {
        return 1;
    }
    else
    {
       return 0;
    }
}*/

/** @brief Process user input and execute requested action
 *
 * This function processes user input and execute requested action
 *
 * @param self pointer to self
 * @User input buffer
 * @return 0
 * @author Yuki
 * @date 14-June-2013
 */
static int Shell_ProcessCommand(shell_t *self, char *buff) {
    int index;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    index = SearchCommand(self, buff);
    if (index >= 0) {
        cmdTable[index].fp(self, buff);
    } else
    {
        if(buff != NULL)
        {
            if (shell_data->fileBuffer!=NULL) //free a previously loaded file that was not executed
               {
                    td_free(shell_data->fileBuffer);
                    shell_data->fileBuffer=NULL;
                }

            shell_data->fileBuffer = buff;
        }
        shell_data->L2P = TRUE;
        ExecuteTests(self, buff);
    }
    return 0;
}

/** @brief Obtains system object
 *
 * This function obtains system object to be accessed by uutshell
 *
 * @param self pointer to self
 * @return Pointer to system object
 * @author Yuki
 * @date 14-June-2013
 */
static system_t* get_system_object(shell_t *self)
{
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    return shell_data->sys;
}

/** @brief Initialises shell
 *
 * This function initialises shell
 *
 * @param self pointer to self
 * @param pointer to shell_params_t
 * @return -1 if initialisation fails otherwise 0
 * @author Yuki
 * @date 14-June-2013
 */
static int shell_init(shell_t *self, shell_params_t *shell_params) {
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;
    system_params_t system_params;

    shell_data->td_reboot = FALSE;

    shell_data->sys = system_create();
    if (!shell_data->sys)
    {
        printf("shell_init: failed\n");
        return -1;
    }

    memset(&system_params, 0, sizeof(system_params_t));
    system_params.uart_uut_device = shell_params->uart_uut_device;
    if (-1 == shell_data->sys->init(shell_data->sys, &system_params))
    {
        /* system init failed */
        return -1;
    }

    shell_data->sys->set_debug_mode(shell_data->sys, td_dbg);
    shell_data->sys->set_print_hardware_lines(shell_data->sys, shell_data->debugPrintHardwareLines);
    shell_exit=0;
    shell_data->mini_txt_timeout = SHELL_MINI_TXT_TIMEOUT;

    return 0;
}



/** @brief Handles interrupt
 *
 * This function handles interrupt for ctrl c, z, and break
 *
 * @param Integer indicating that an interrupt is sent
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void shell_signal_handler(int signum)
{
  shell_exit = 1;
}

/** @brief Handles user inputs and executes command
 *
 * This function handles user inputs and executes command
 *
 * @param self pointer to self
 * @return 1 on request to exit
 * @author Yuki
 * @date 14-June-2013
 */
static int shell_run(shell_t *self)
{
    char *buff;
    int r;
    size_t bytes_read, nbytes;
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    /* install CTRL-C signal handler */
    signal(SIGINT, shell_signal_handler);

    uart_t *uartUUT = shell_data->sys->get_uart_uut(shell_data->sys);
    S_internal_WriteAndFlush(uartUUT, "", 0);

    /* main loop */
    r = 0;
    fflush(stdout);

    while ((td_exit==FALSE)&&(shell_data->td_reboot==FALSE))
    {
        Shell_PrintPrompt(self);

        buff = NULL;
        bytes_read = getline(&buff, &nbytes, stdin);
        if (td_dbg==DEBUG_MODE_ON)
            printf("[getline_malloc] ptr=%p\t size=0x%zx\t \n", buff, nbytes);

        // TODO: handle CTRL+C in a clean way, currently pressing CTRL+C doesn't immediately return, pressing enter is required because of previous getline(), this needs to be fixed
        if (shell_exit==1)
        {
            shell_exit=0;
            if (buff !=NULL)
            {
                 td_free(buff);
                 buff=NULL;
            }
            break;
        }

        if ((bytes_read > 0) && (Parser_IsBlankLine(buff)==0))
        {
            r = Shell_ProcessCommand(self, buff);
            fflush(stdout);
        }

       if (buff !=NULL)
       {
            td_free(buff);
            buff=NULL;
       }

    }

    return r;
}

/** @brief Destroys shell object and frees memory allocated
 *
 * This function handles user requests and executes any command
 *
 * @param self pointer to self
 * @return None
 * @author Yuki
 * @date 14-June-2013
 */
static void shell_destroy(shell_t *self)
{
    shell_private_data_t *shell_data = (shell_private_data_t *) self->_private;

    if (shell_data->sys)
    {
        shell_data->sys->destroy(shell_data->sys);
        shell_data->sys = NULL;
    }

    if(shell_data->fileBuffer !=NULL)
    {
       td_free(shell_data->fileBuffer);
       shell_data->fileBuffer=NULL;
    }

    if (shell_data !=NULL)
    {
        td_free(shell_data);
        shell_data=NULL;
    }
    td_free(self);
    self=NULL;
}

/** @brief Create shell object and initialises  private data
 *
 * This function handles user requests and executes any command
 *
 * @return Pointer to shell or NULL if not successful
 * @author Yuki
 * @date 14-June-2013
 */
shell_t* shell_create()
{
    shell_t* self = td_malloc(sizeof(shell_t));
    //printf("%p\n", self);
    if (!self)
    {
        printf("shell_create: Failed to allocate memory, %s:%d\n", __FILE__,__LINE__);
        return NULL;
    }

    memset(self, 0, sizeof(shell_t));

    shell_private_data_t *shell_private_data = td_malloc(sizeof(shell_private_data_t));
    if (!shell_private_data)
    {
        printf("shell_create: Failed to allocate memory, %s:%d\n", __FILE__,__LINE__);
        td_free(self);
        self=NULL;
        return NULL;
    }

    memset(shell_private_data, 0, sizeof(shell_private_data_t));
    /* set up function pointers */
    self->_private = shell_private_data;
    self->init = shell_init;
    self->run = shell_run;
    self->destroy = shell_destroy;
    self->get_system_object = get_system_object;
    return self;
}

