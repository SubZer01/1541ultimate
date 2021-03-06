extern "C" {
	#include "itu.h"
    #include "dump_hex.h"
    #include "small_printf.h"
}
#include "usb_base.h"
#include "usb_device.h"
#include "task.h"
#include "profiler.h"

#include <stdlib.h>
#include <string.h>

#ifdef NIOS
#define nano_word(x) ((x >> 8) | ((x & 0xFF) << 8))
#else // assume big endian
#define nano_word(x) x
#endif

UsbBase usb2;

#if DISABLE_USB_DEBUG
    #define printf(...)
#endif

UsbBase :: UsbBase()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
	max_current = 500;
	remaining_current = 500;
	rootDevice = NULL;
	initialized = false;
	bus_speed = -1;
	setupBuffer = new uint8_t[8];
//	initHardware();
}

UsbBase :: ~UsbBase()
{
	deinitHardware();
}

UsbDevice *UsbBase :: first_device(void)
{
    return rootDevice;
}

/*
void UsbBase :: clean_up()
{
	rootDevice->deinstall();

	for(int i=0; i<128; i++) {
		deviceUsed[i] = 0;
	}
}
*/

// Called only from the Event context
int UsbBase :: get_device_slot(void)
{
    for(int i=1;i<128;i++) {
    	if(!deviceUsed[i]) {
    		deviceUsed[i] = 1;
    		return i;
    	}
    }
    return -1; // no empty slots
}
    

// Called only from the Event context
void UsbBase :: attach_root(void)
{
    char buf[16];
    printf("Attach root!!\n");
    rootDevice = NULL;
    remaining_current = 500; //max_current;
    bus_reset();

    UsbDevice *dev = new UsbDevice(this, get_bus_speed());
    if (init_device(dev)) {
    	rootDevice = dev;
    }
    if (!rootDevice->init2()) {
    	printf("Failed to initialize device %s\n", dev->get_pathname(buf, 16));
    	return;
    }
    rootDevice->install(); // should actually call install_device, so that current can be checked.
}

// Called only from the Event context
bool UsbBase :: init_device(UsbDevice *dev) // This function only gives the device an address on the USB
{
	int idx = get_device_slot();
    char buf[16];
    dev->get_pathname(buf, 16);
    printf("Installing %s Parent = %p, ParentPort = %d\n", buf, dev->parent, dev->parent_port);

    bool ok = false;
    for(int i=0;i<5;i++) { // try 5 times!
        if(dev->init(idx)) {
            ok = true;
            break;
        } else {
        	wait_ms(100*i);
        }
    }
    return ok;
}

bool UsbBase :: install_device(UsbDevice *dev, bool draws_current)
{
    if(!draws_current) {
        dev->install();
        return true; // actually install should be able to return false too
    } else {
        int device_curr = int(dev->get_device_config()->max_power) * 2;

        if(device_curr > remaining_current) {
        	printf("Device current (%d mA) exceeds maximum remaining current (%d mA).\n", device_curr, remaining_current);
        }
        remaining_current -= device_curr;
        dev->install();
        return true; // actually install should be able to return false too
    }
    return false;
}

// Called from cleanup task.
void UsbBase :: deinstall_device(UsbDevice *dev)
{
    int addr = dev->current_address;
	dev->deinstall();
	delete dev;
	deviceUsed[addr] = 0;
}

////////////////////////////

extern uint8_t _nano_minimal_b_start;
extern uint32_t _nano_minimal_b_size;

uint16_t *attr_fifo_data = (uint16_t *)ATTR_FIFO_BASE;

static void poll_usb2(void *a)
{
	usb2.init();
	while(1) {
		usb2.poll();
	}
}

extern "C" BaseType_t usb_irq() {
	BaseType_t xHigherPriorityTaskWoken = usb2.irq_handler();
	// portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	return xHigherPriorityTaskWoken;
}

void UsbBase :: initHardware()
{
	irq_count = 0;
	prev_status = 0xFF;

    if(getFpgaCapabilities() & CAPAB_USB_HOST2) {
    	mutex = xSemaphoreCreateMutex();
    	enumeration_lock = false;
    	queue = xQueueCreate(16, sizeof(uint16_t));
        cleanup_queue = xQueueCreate(16, sizeof(UsbDevice *));
        commandSemaphore = xSemaphoreCreateBinary();
        NANO_START = 0;
        uint16_t *dst = (uint16_t *)NANO_BASE;
        for(int i=0; i<2048; i+=2) {
        	*(dst++) = 0;
        }
        ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
        ioWrite8(ITU_IRQ_CLEAR, ITU_INTERRUPT_USB);

        xTaskCreate( poll_usb2, "USB Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 1, NULL );
    } else {
        printf("No USB2 hardware found.\n");
    }
}

void UsbBase :: deinitHardware()
{
    ioWrite8(ITU_IRQ_DISABLE, ITU_INTERRUPT_USB);
	vSemaphoreDelete(mutex);
	vSemaphoreDelete(commandSemaphore);
	vQueueDelete(queue);
    //clean_up();
	vQueueDelete(cleanup_queue);
}

// This STARTS the event context
void UsbBase :: input_task_start(void *u)
{
	UsbBase *usb = (UsbBase *)u;
	usb->input_task_impl();
}

// This RUNS the event context
void UsbBase :: input_task_impl(void)
{
	printf("USB input task is now running. this = %p\n", this);
	uint16_t fifoWord;

	while(1) {
		if (!queue) {
			printf("Bleh!\n");
			vTaskDelay(10);
			continue;
		}
		if (xQueueReceive(queue, &fifoWord, 5000) == pdTRUE) {
			// printf("{%04x:%04x}", ev.fifo_word[0], ev.fifo_word[1]);
			PROFILER_SUB = 5;
			process_fifo(fifoWord);
		} else {
			printf("@");
		}
	}
}

// Called from event context
void UsbBase :: process_fifo(uint16_t pipe)
{
	int error = 0;

	if ((pipe & 0xFFF0) == 0xFFF0) {
	    uint16_t status = pipe & 0xF;
	    printf("USB Other IRQ. Status = %b\n", status);
        handle_status(status);
        return;
    }

/*
			// pause_input_pipe(pipe);
			printf("USB Pipe Error. Pipe %d: Status: %4x\n", pipe, ev->fifo_word[1]);
			void *object = inputPipeObjects[pipe];
			if (object) {
				((UsbDriver *)object)->pipe_error(pipe);
			}
			return;
		}
*/

	void *object = inputPipeObjects[pipe];

	PROFILER_SUB = 3;
	if (!object) {
		printf("** INVALID USB PACKET RECEIVED IN QUEUE! Pipe : %d\n", pipe);
		return;
	}
	inputPipeCallBacks[pipe](object);
	PROFILER_SUB = 12;
}

// Called from event context
void UsbBase :: handle_status(uint16_t new_status)
{
	if (prev_status != new_status) {
		prev_status = new_status;
		if (new_status & USTAT_CONNECTED) {
			attach_root();
        } else {
        	if (rootDevice) {
//        		rootDevice->disable();
				queueDeinstall(rootDevice);
				rootDevice = NULL;
        	}
		}
    }
}

// Called from event context
void UsbBase :: queueDeinstall(UsbDevice *device)
{
	device->disable(); // do no more transactions, disable poll pipes, etc
	if (!cleanup_queue) {
		printf("CUleh!\n");
		return;
	}
	xQueueSend(cleanup_queue, &device, 500000);
}

// Called during init?  TODO
void UsbBase :: bus_reset()
{
	NANO_DO_RESET = 1;

	for (int i=0; i<3; i++) {
		wait_ms(50);
		printf("Reset status: %b Speed: %b\n", USB2_STATUS, NANO_LINK_SPEED);
		if (USB2_STATUS & USTAT_OPERATIONAL)
			break;
	}
	set_bus_speed(NANO_LINK_SPEED);
}


///////////////////////////////////////////////////////
// Called from poll / cleanup context
void UsbBase :: init(void)
{
    // clear our internal device list
    for(int i=0;i<128;i++) {
        deviceUsed[i] = 0;
    }
    rootDevice = NULL;

	NANO_START = 0;

    // load the nano CPU code and start it.
    int size = (int)&_nano_minimal_b_size;
    uint8_t *src = &_nano_minimal_b_start;
    uint16_t *dst = (uint16_t *)NANO_BASE;
    uint16_t temp;
    for(int i=0;i<size;i+=2) {
    	temp = uint16_t(*(src++));
        temp |= (uint16_t(*(src++)) << 8);
    	*(dst++) = temp;
	}
    for(int i=size;i<2048;i+=2) {
    	*(dst++) = 0;
    }
    uint32_t *pul = (uint32_t *)NANO_BASE;
    uint32_t ul = *pul;
    printf("Nano CPU based USB Controller: %d bytes loaded from %p. First DW: %08x\n", size, &_nano_minimal_b_start, ul);

	ioWrite8(ITU_IRQ_CLEAR,  0x04);
	ioWrite8(ITU_IRQ_ENABLE, 0x04);
	NANO_START = 1;

    printf("Queue = %p. Creating USB task. This = %p\n", queue, this);
	xTaskCreate( UsbBase :: input_task_start, "USB Input Event Task", configMINIMAL_STACK_SIZE, this, tskIDLE_PRIORITY + 4, NULL );

	ioWrite8(ITU_MISC_IO, 1); // coherency is on
}

// Called from poll / cleanup context
void UsbBase :: poll()
{
	UsbDevice *device;
	BaseType_t doCleanup;

	do {
		if (!cleanup_queue) {
			printf("CURBleh\n");
		}
		doCleanup = xQueueReceive(cleanup_queue, &device, 40);
		if (doCleanup == pdTRUE) {
			deinstall_device(device);
		}
	} while(doCleanup == pdTRUE);

	if(rootDevice) {
		rootDevice->poll();
	}
}

void UsbBase :: deinit(void)
{
	//power_off();

	NANO_START = 0;

	// clear RAM
	uint8_t *dst = (uint8_t *)NANO_BASE;
    for(int i=0;i<2048;i++)
        *(dst++) = 0;
}

// Called from protected function 'allocate input pipe'
int UsbBase :: open_pipe()
{
    volatile t_usb_descriptor *descr = &USB2_DESCRIPTORS[1];

	for(int i=1;i<USB2_NUM_PIPES;i++) {
		if (descr->command == 0) {
		    if (NANO_NUM_PIPES < (i+1)) {
		        NANO_NUM_PIPES = i+1;
		    }
		    return i;
		}
		descr++;
	}
	return -1;
}

// Called from protected function 'allocate input pipe'
void UsbBase :: init_pipe(int index, struct t_pipe *init)
{
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(index);

    descr->devEP    = init->DevEP;
	descr->maxTrans = init->MaxTrans;
	descr->interval = init->Interval;
	descr->splitCtl = init->SplitCtl;
	descr->command  = 0;

	// the rest is set by the resume command (which should maybe be called trigger)

	if (init->Command) {
		descr->command = init->Command | UCMD_PAUSED;
	} else {
		uint16_t command = UCMD_MEMWRITE | UCMD_DO_DATA | UCMD_IN;
		init->Command = command;
		descr->command = command | UCMD_PAUSED;
	}
}

// Still used? -> Yes
void UsbBase :: pause_input_pipe(int index)
{
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(index);

	uint16_t command = descr->command;
	if (command & UCMD_PAUSED) {
		printf("Pause was already set %04x\n", command);
		return;
	}
	command |= UCMD_PAUSED;
	descr->command = command;
}

// Called from event context (pipe input processor)
void UsbBase :: resume_input_pipe(int index)
{
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(index);
    struct t_pipe *init = inputPipeDefinitions[index];

    descr->length   = init->Length;
    uint32_t address = (uint32_t)init->buffer;
    descr->memHi    = address >> 16;
    descr->memLo    = address & 0xFFFF;
    descr->started  = 0;

    uint16_t command = descr->command;
	command &= ~UCMD_PAUSED;
    descr->command = command;
	//printf("Input pipe %d resumed: %04x\n", index, pipe[PIPE_OFFS_Command]);
}

void UsbBase :: close_pipe(int pipe)
{
    volatile t_usb_descriptor *descr = &USB2_DESCRIPTORS[pipe];
    descr->command = 0;
}

uint16_t UsbBase :: complete_command(int timeout)
{
	uint16_t result;

    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);

	if (! xSemaphoreTake(commandSemaphore, timeout)) {
	    pause_input_pipe(0);
	    result = descr->result | URES_ABORTED;
	} else {
		result = descr->result & ~URES_ABORTED;
	}
	return result;
}

// Reentrant
uint16_t UsbBase :: getSplitControl(int addr, int port, int speed, int type)
{
	uint16_t retval = SPLIT_DO_SPLIT | (addr << 8) | (port & 0x0F) | ((type & 0x03) << 4);
	switch(speed) {
	case 1:
		break;
	case 0:
		retval |= SPLIT_SPEED;
		break;
	default:
		retval = 0;
	}
	return retval;
}

// Protected by Mutex
void UsbBase :: power_off()
{
/*
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return;
    }

    printf("Turning USB bus off...\n");
    descr->command = UCMD_OFF;
    vTaskDelay(10);
    descr->command = 0;

    xSemaphoreGive(mutex);
*/
}

void UsbBase :: doPing(struct t_pipe *pipe)
{
	if (!(pipe->highSpeed)) {
		pipe->needPing = 0;
		return;
	}

	uint16_t result;
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);
	do {
		descr->devEP   = pipe->DevEP;
		descr->length  = 0;
		descr->command = UCMD_PING;
		descr->started = 0;
		result = complete_command(100);
	} while ((result & URES_RESULT_MSK) == URES_NAK);
	pipe->needPing = 0;
}

// Protected by Mutex
int UsbBase :: control_exchange(struct t_pipe *pipe, void *out, int outlen, void *in, int inlen)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }

    if (outlen != 8) {
        printf("Unsupported setup length.\n");
        return -10;
    }

    memcpy(setupBuffer, out, 8);
    uint32_t outAddress = (uint32_t)setupBuffer;

    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);
    descr->devEP  = pipe->DevEP;
	descr->length = outlen;
	descr->maxTrans = pipe->MaxTrans;
	descr->memHi = outAddress >> 16;
	descr->memLo = outAddress & 0xFFFF;
	descr->splitCtl = pipe->SplitCtl;
	descr->started  = 0;

	uint16_t result = 0;
	descr->command = UCMD_MEMREAD | UCMD_DO_DATA | UCMD_SETUP;

	result = complete_command(100);
	if ((result & URES_RESULT_MSK) != URES_ACK) {
		printf("Setup Result: %04x\n", result);
	    xSemaphoreGive(mutex);
		return -1;
	}

	descr->length = inlen;
	uint16_t command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT;// start with toggle bit 1

	if (in) {
		descr->memHi = ((uint32_t)in) >> 16;
		descr->memLo = ((uint32_t)in) & 0xFFFF;
		command |= UCMD_MEMWRITE;
	}
    descr->started = 0;
	descr->command = command;

	result = complete_command(100);

	uint32_t transferred = inlen - descr->length;
	// printf("In: %d bytes (Result = %4x)\n", transferred, result);
	// dump_hex(read_buf, transferred);

	if ((result & URES_RESULT_MSK) == URES_STALL) {
	    xSemaphoreGive(mutex);
		return -4;
	}

	if (transferred > 0) {
		descr->length = 0;
	    descr->started = 0;
		descr->command = UCMD_MEMREAD | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT | UCMD_RETRY_ON_NAK; // send zero bytes as out packet

		result = complete_command(100);

		switch (result & URES_RESULT_MSK) {
		case URES_ACK:
			// OK
			break;
		case URES_NYET:
			// OK, but need ping next time if we are in high speed mode
			pipe->needPing = 1;
			break;
		default:
			printf("Control status phase Out error: $%4x\n", result);
			break;
		}
	}
    xSemaphoreGive(mutex);
	return transferred;
}

// Protected by Mutex
int UsbBase :: control_write(struct t_pipe *pipe, void *setup_out, int setup_len, void *data_out, int data_len)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);
	descr->devEP  = pipe->DevEP;
	descr->length = setup_len;
	descr->maxTrans = pipe->MaxTrans;
	descr->memHi = ((uint32_t)setup_out) >> 16;
	descr->memLo = ((uint32_t)setup_out) & 0xFFFF;
	descr->splitCtl = pipe->SplitCtl;
	descr->started = 0;
	descr->command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_SETUP;

	uint16_t result = complete_command(100);

	// printf("Setup Result: %04x\n", USB2_CMD_Result);

	descr->length = data_len;
	descr->memHi = ((uint32_t)data_out) >> 16;
	descr->memLo = ((uint32_t)data_out) & 0xFFFF;
    descr->started = 0;
	descr->command = UCMD_MEMREAD | UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_OUT | UCMD_TOGGLEBIT; // start with toggle bit 1;

	result = complete_command(100);

	uint32_t transferred = data_len - descr->length;
	// printf("Out: %d bytes (Result = %4x)\n", transferred, USB2_CMD_Result);
	// dump_hex(read_buf, transferred);

	descr->length = 0;
    descr->started = 0;
	descr->command = UCMD_RETRY_ON_NAK | UCMD_DO_DATA | UCMD_IN | UCMD_TOGGLEBIT; // do an in transfer to end control write

	result = complete_command(100);

	// printf("In Result = %4x\n", USB2_CMD_Result);
    xSemaphoreGive(mutex);
	return transferred;
}

// Protected by Mutex
int  UsbBase :: allocate_input_pipe(struct t_pipe *pipe, usb_callback callback, void *object)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	int index = open_pipe();
	if (index > 0) {
		inputPipeCallBacks[index] = callback;
		inputPipeObjects[index] = object;
		inputPipeDefinitions[index] = pipe;

		init_pipe(index, pipe);
	}
/*
	for(int i=0;i<8;i++) {
		printf(" OBJ: %p CB: %p CMB: %4x\n", inputPipeObjects[i], inputPipeCallBacks[i], inputPipeCommand[i]);
	}
*/
	xSemaphoreGive(mutex);
	return index;
}

int  UsbBase :: getReceivedLength(int index)
{
    volatile t_usb_descriptor *descr = &USB2_DESCRIPTORS[index];
    struct t_pipe *pipe = inputPipeDefinitions[index];
    return (pipe->Length) - (descr->length);
}

void UsbBase :: free_input_pipe(int index)
{
	if (index < 0) {
		return;
	}
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return;
    }
	close_pipe(index);
	inputPipeCallBacks[index] = 0;
	inputPipeObjects[index] = 0;
    xSemaphoreGive(mutex);
}

int  UsbBase :: bulk_out(struct t_pipe *pipe, void *buf, int len, int timeout)
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }
	// printf("BULK OUT to %4x, len = %d\n", pipe->DevEP, len);

	uint8_t sub = PROFILER_SUB; PROFILER_SUB = 13;
    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);

/*
    if (((uint32_t)buf) & 3) {
    	printf("Bulk_out: Unaligned buffer %p (%d - len = %d)\n", buf, ((uint32_t)buf) & 3, len);
    	dump_hex((void *)(((uint32_t)buf) & -4), 16);
    	//while(1);
    }
*/

    uint32_t addr = ((uint32_t)buf);
	int total_trans = 0;

	uint16_t result;
	int current_len = 0;

	do {
		current_len = (len > 49152) ? 49152 : len;
		descr->devEP    = pipe->DevEP;
		descr->maxTrans = pipe->MaxTrans;
		descr->splitCtl = pipe->SplitCtl;
		descr->memHi    = addr >> 16;
		descr->memLo    = addr & 0xFFFF;
		descr->length   = current_len;

		uint16_t cmd = pipe->Command | UCMD_MEMREAD | UCMD_DO_DATA | UCMD_OUT | UCMD_RETRY_ON_NAK;
		//printf("%d: %4x ", len, cmd);

		descr->started = 0;
		descr->command = cmd;
		// wait until it gets zero again
		result = complete_command(timeout);

		pipe->Command = (descr->command & URES_TOGGLE); // that's what we start with next time.

		if (((result & URES_RESULT_MSK) != URES_ACK) && ((result & URES_RESULT_MSK) != URES_NYET)) {
			printf("Bulk Out error: $%4x, Transferred: %d\n", result, total_trans);
			break;
		}

		//printf("Res = %4x\n", result);
		int transferred = current_len - descr->length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;

	} while (len > 0);

    // printf("Bulk out done: %d\n", total_trans);
    xSemaphoreGive(mutex);

    PROFILER_SUB = sub;
	return total_trans;
}

int  UsbBase :: bulk_in(struct t_pipe *pipe, void *buf, int len, int timeout) // blocking
{
    if (!xSemaphoreTake(mutex, 5000)) {
    	printf("USB unavailable.\n");
    	return -9;
    }

    if (((uint32_t)buf) & 3) {
    	printf("Bulk_in: Unaligned buffer %p\n", buf);
    	while(1);
    }

    volatile t_usb_descriptor *descr = USB2_DESCRIPTOR(0);

    // printf("BULK IN from %4x, len = %d\n", pipe->DevEP, len);
	uint32_t addr = (uint32_t)buf;
	int total_trans = 0;

	descr->devEP    = pipe->DevEP;
	descr->maxTrans = (len > pipe->MaxTrans) ? pipe->MaxTrans : len;
	descr->splitCtl = pipe->SplitCtl;
	descr->memHi = addr >> 16;
	descr->memLo = addr & 0xFFFF;

	do {
		int current_len = (len > 49152) ? 49152 : len;
		descr->length = current_len;
		uint16_t cmd = pipe->Command | UCMD_MEMWRITE | UCMD_DO_DATA | UCMD_IN | UCMD_RETRY_ON_NAK;

		//printf("%d: %4x \n", len, cmd);
		descr->started = 0;
		descr->command = cmd;
		uint16_t result = complete_command(timeout);

		if ((result & URES_RESULT_MSK) != URES_PACKET) {
			printf("Bulk IN error: $%4x, Transferred: %d\n", result, total_trans);
			if ((result & URES_RESULT_MSK) == URES_STALL) {
				total_trans = -4; // error code
			} else {
				total_trans = -1; // error code
			}
			break;
		}

		//printf("Res = %4x\n", result);
		int transferred = current_len - descr->length;
		total_trans += transferred;
		addr += transferred;
		len -= transferred;
		pipe->Command = descr->command & URES_TOGGLE; // that's what we start with next time.
		if (transferred != current_len) { // some bytes remained?
			//printf("CMD res: %4x\n", result);
			//dump_hex_relative(buf, transferred);
			//total_trans = -8;
			break;
		}
	} while (len > 0);

    // printf("Bulk in done: %d\n", total_trans);
    xSemaphoreGive(mutex);

    return total_trans;
}

/*
UsbDevice *UsbBase :: init_simple(void)
{
	init();
	bus_reset();
	wait_ms(1000);
	uint8_t usb_status = USB2_STATUS;
	if (usb_status & USTAT_CONNECTED) {
		UsbDevice *dev = new UsbDevice(this, get_bus_speed());
		if(dev) {
			if(!dev->init(1)) {
				delete dev;
				return NULL;
			}
		}
		return dev;
	}
	return NULL;
}
*/

////////////////////////////////////////////////////////////

// Called from IRQ
BaseType_t UsbBase :: irq_handler(void)
{
	PROFILER_SUB = 2;
	irq_count++;
	uint16_t read;

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	while (get_fifo(&read)) {
		if (read == 0x0000) {
			xSemaphoreGiveFromISR(commandSemaphore, &xHigherPriorityTaskWoken);
			continue;
		} else {
		    if (!queue) {
				ioWrite8(UART_DATA, '#');
			} else {
				xQueueSendFromISR(queue, &read, &xHigherPriorityTaskWoken);
			}
		}
	}
	PROFILER_SUB = 0;
	return xHigherPriorityTaskWoken;
}

// Called from IRQ
bool UsbBase :: get_fifo(uint16_t *out)
{
	uint16_t tail = ATTR_FIFO_TAIL;
	uint16_t head;

	do {
		head = ATTR_FIFO_HEAD;
	} while(head != ATTR_FIFO_HEAD);

//	printf("Head: %d, Tail: %d\n", head, tail);
	if (tail == head) {
		*out = 0xFFFF;
		return false;
	}
	*out = attr_fifo_data[tail];
	tail ++;
	if (tail == ATTR_FIFO_ENTRIES)
		tail = 0;
	ATTR_FIFO_TAIL = tail;
	return true;
}
