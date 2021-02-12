/*
 * main_cpu0.c
 *
 *  Created on: May 14, 2019
 *      Author: xuantran
 */


/*****************************************************************************/
#include "string.h"

#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

#include "xil_printf.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_exception.h"
#include "xpseudo_asm.h"
#include "xscugic.h"

/*****************************************************************************/

#define sev() __asm__("sev")
#define CPU1STARTADR		0xFFFFFFF0

#define INTC				XScuGic
#define INTC_DEVICE_ID		XPAR_PS7_SCUGIC_0_DEVICE_ID

#define SW_INT_ID_0			14

static const uint32_t CONTROL_MESSAGE_BUFFER_SIZE = 24;
static const uint32_t TASK_MESSAGE_BUFFER_SIZE = 64;

static StreamBufferHandle_t volatile *controlMessgeBufferStorage = (StreamBufferHandle_t *) (0xFFFF0000);
static uint8_t volatile *ucBufferStorage = (uint8_t *) (0xFFFF1000);

static char *pc10ByteString = "0123456780";
static const uint32_t DELAY_1_MS = 100UL;

/*****************************************************************************/
static MessageBufferHandle_t xMessageBuffer;
static StaticMessageBuffer_t xMessageBufferStruct;
static INTC IntcInstancePtr;
/*****************************************************************************/
static void sgiHandler0 (void *CallBackRef);
static int32_t setupIntrSystem (INTC *IntcInstancePtr);

static void txTask (void *pvParameters);

void vGenerateCore1Interrupt (void * xControlMessageBuffer);
/*****************************************************************************/
int main(void){

	BaseType_t taskStatus = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;

	int32_t status = XST_FAILURE;

	/* Disable cache on OCM */
	/* S = b1, TEX = b100, AP = b11, Domain = b1111, C = b0, B = b0 */
	Xil_SetTlbAttributes (0xFFFF0000, 0x14de2);

	Xil_Out32 (CPU1STARTADR, 0x00200000);
	dmb(); /* wait until write has finished */

	sev();

	status = setupIntrSystem (&IntcInstancePtr);
	if (XST_FAILURE == status) {
		xil_printf ("CPU0: Cannot set up the Interrupt System.\r\n");
	}

	/* Message Buffer for task message */
	xMessageBuffer = xMessageBufferCreateStatic (TASK_MESSAGE_BUFFER_SIZE,
													ucBufferStorage,
													&xMessageBufferStruct);
	if (xMessageBuffer == NULL) {
		xil_printf ("Cannot create a static message buffer.\r\n");
	}

	taskStatus = xTaskCreate (txTask, 				/* The function that implements the task. */
						( const char * ) "Tx", 		/* Text name for the task, provided to assist debugging only. */
						configMINIMAL_STACK_SIZE, 	/* The stack allocated to the task. */
						(void *)xMessageBuffer, 	/* Message Buffer */
						tskIDLE_PRIORITY +1,			/* The task runs at the idle priority. */
						NULL);
	if (pdPASS != taskStatus) {
		xil_printf ("Cannot create a task because there is insufficient heap"
				" memory available for FreeRTOS to allocate the task data"
				" structures and stack.\r\n");
	}

	/* Start the tasks and timer running. */
	vTaskStartScheduler();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached.  If the following line does execute, then there was
	insufficient FreeRTOS heap memory available for the idle and/or timer tasks
	to be created.  See the memory management section on the FreeRTOS web site
	for more details. */
	for( ;; );

	return 0;
}
/*****************************************************************************/

static void sgiHandler0 (void *CallBackRef) {}

/*****************************************************************************/
static void txTask (void *pvParameters)
{
	int32_t status = XST_FAILURE;
	MessageBufferHandle_t xMessageBuffer;
	xMessageBuffer = (MessageBufferHandle_t) pvParameters;
	volatile StreamBufferHandle_t xControlMessageBuffer;
	size_t xBytesToSend;
	size_t xBytesActuallySent;
	const size_t xStringLength = strlen(pc10ByteString);
	const TickType_t x1msecond = pdMS_TO_TICKS( DELAY_1_MS );
	const size_t xExpectedSpace = 14;
	size_t xReturnedSpace = 0;

	xBytesToSend = xStringLength;

	xControlMessageBuffer = (StreamBufferHandle_t) xMessageBuffer;

	/* Copy the message buffer handle to SHM */
	memcpy (controlMessgeBufferStorage,
			&xControlMessageBuffer,
			sizeof(xControlMessageBuffer));

	for(;;) {

		xReturnedSpace = xMessageBufferSpaceAvailable (*controlMessgeBufferStorage);
		if (xReturnedSpace >= xExpectedSpace) {
			/* Send message to the message buffer */
			xBytesActuallySent = xMessageBufferSend (*controlMessgeBufferStorage,
													pc10ByteString,
													xBytesToSend,
													0);
			if (xBytesActuallySent == xBytesToSend) {

				/* Generate an interrupt to the other core */
				status = XScuGic_SoftwareIntr (&IntcInstancePtr,
												SW_INT_ID_0,
												XSCUGIC_SPI_CPU1_MASK);
				if (XST_SUCCESS != status) {
					xil_printf ("Cannot trigger SGI.\r\n");
				}

				if (*(pc10ByteString + 9) == '9') {
					*(pc10ByteString + 9) -= 10;
				}
				/* Increase the value of string */
				*(pc10ByteString + 9) += 1;

			}
		}
	}
}

/*****************************************************************************/
static int32_t setupIntrSystem (INTC *IntcInstancePtr)
{
	int32_t status = XST_FAILURE;

	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig (INTC_DEVICE_ID);
	if (NULL == IntcConfig){
		return XST_FAILURE;
	}

	status = XScuGic_CfgInitialize (IntcInstancePtr,
									IntcConfig,
									IntcConfig->CpuBaseAddress);
	if (XST_SUCCESS != status) {
		return XST_FAILURE;
	}
	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler (XIL_EXCEPTION_ID_INT,
								(Xil_ExceptionHandler) XScuGic_InterruptHandler,
								IntcInstancePtr);
	status = XScuGic_Connect (IntcInstancePtr,
							SW_INT_ID_0,
							(Xil_ExceptionHandler) sgiHandler0,
							(void*) IntcInstancePtr);
	if (XST_SUCCESS != status) {
		return XST_FAILURE;
	}

	XScuGic_Enable (IntcInstancePtr, SW_INT_ID_0);

	Xil_ExceptionEnableMask (XIL_EXCEPTION_IRQ);

	return status;
}

/*****************************************************************************/
