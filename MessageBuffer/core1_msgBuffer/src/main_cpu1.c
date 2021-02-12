/*
 * main_cpu1.c
 *
 *  Created on: May 14, 2019
 *      Author: xuantran
 */

/*****************************************************************************/
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "message_buffer.h"

#include "xil_io.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "xil_printf.h"

/*****************************************************************************/
#define INTC_DEVICE_ID	XPAR_PS7_SCUGIC_0_DEVICE_ID
#define INTC			XScuGic
#define SW_INT_ID_0		14

static char *pc10ByteString = "/0";

static const uint32_t CONTROL_MESSAGE_BUFFER_SIZE = 24;
static const uint32_t TASK_MESSAGE_BUFFER_SIZE = 64;
static StreamBufferHandle_t volatile *controlMessageBufferStorage = (StreamBufferHandle_t *) (0xFFFF0000);
static uint8_t volatile *ucBufferStorage = (uint8_t *) (0xFFFF1000);

/*****************************************************************************/

/*****************************************************************************/
static void sgiHandler0 (void *CallBackRef);
static int32_t setupIntrSystem (INTC *GicInstancePtr);

static void rxTask (void *pvParameters);

/*****************************************************************************/
int main (void) {
	int32_t status = XST_FAILURE;
	INTC IntcInstancePtr;
	BaseType_t taskStatus = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;

	/* Disable cache on OCM */
	/* S = b1, TEX = b100, AP = b11, Domain = b1111, C = b0, B = b0 */
	Xil_SetTlbAttributes (0xFFFF0000, 0x14de2);

	status = setupIntrSystem (&IntcInstancePtr);
	if (XST_FAILURE == status) {
		xil_printf ("CPU1: Cannot set up the Interrupt System.\r\n");
	}
	taskStatus = xTaskCreate (rxTask, 				/* The function that implements the task. */
						( const char * ) "Rx", 		/* Text name for the task, provided to assist debugging only. */
						configMINIMAL_STACK_SIZE, 	/* The stack allocated to the task. */
						NULL, 						/* There is no parameter */
						tskIDLE_PRIORITY,			/* The task runs at the idle priority. */
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
static void sgiHandler0 (void *CallBackRef)
{
	uint32_t INT = 0x0;
	StreamBufferHandle_t xUpdatedControlMessageBuffer;
	uint8_t rxData[16] = {0};
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	size_t xReceiveLength = 0;
	/* Read ICCIAR
	 * This read acts as an acknowledge for the interrupt
	 */
	INT = Xil_In32 (0xF8F0010C);

	/* Copy information of the control message buffer from the SHM */

	if (pdFALSE == xMessageBufferIsEmpty((MessageBufferHandle_t)(*controlMessageBufferStorage)))
	{
		/* Receive message from the message buffer */
		xReceiveLength = xMessageBufferReceiveFromISR ((MessageBufferHandle_t)(*controlMessageBufferStorage),
												rxData,
												sizeof(rxData),
												&xHigherPriorityTaskWoken);
	}

	if (0 >= xReceiveLength) {
		xil_printf ("Do not receive successfully.\r\n");
	} else {
		xil_printf ("Received data: %s\r\n", rxData);
	}

	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
/*****************************************************************************/
static void rxTask (void *pvParameters)
{
	for (;;) {}
}
/*****************************************************************************/
static int32_t setupIntrSystem (INTC *GicInstancePtr)
{
	int32_t status = XST_FAILURE;
	XScuGic_Config *IntcConfig; // GIC Config
	Xil_ExceptionInit();
	/* Initialize the GIC */
	IntcConfig = XScuGic_LookupConfig (INTC_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}
	XScuGic_CfgInitialize (GicInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress);
	/* Connect to the hardware */
	Xil_ExceptionRegisterHandler (XIL_EXCEPTION_ID_INT,
								(Xil_ExceptionHandler)XScuGic_InterruptHandler,
								GicInstancePtr);
	status =  XScuGic_Connect (GicInstancePtr,
								SW_INT_ID_0,
								(Xil_ExceptionHandler) sgiHandler0,
								(void*)GicInstancePtr);
	if (XST_SUCCESS != status) {
		return XST_FAILURE;
	}

	XScuGic_Enable (GicInstancePtr, SW_INT_ID_0);

	Xil_ExceptionEnableMask (XIL_EXCEPTION_IRQ);

	return status;
}
/*****************************************************************************/
