#ifndef __LINUX_ES305B_PARAM_H_
#define __LINUX_ES305B_PARAM_H_

static const u8 incall_receiver_buf[] =
{
	// select path route 0x0001
	0x80, 0x26, 0x00, 0x01,

	0x80, 0x0C, 0x0A, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0A, 0x02, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0000: (0 clocks)
	0x80, 0x0C, 0x0A, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0A, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0A, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0A, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0A, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM

	0x80, 0x0C, 0x0C, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0C, 0x02, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0C, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0C, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0C, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM
};

static const u8 incall_speaker_buf[] =
{
	// select path route 0x0001
	0x80, 0x26, 0x00, 0x01,

	0x80, 0x0C, 0x0A, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0A, 0x02, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0000: (0 clocks)
	0x80, 0x0C, 0x0A, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0A, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0A, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0A, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0A, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM

	0x80, 0x0C, 0x0C, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0C, 0x02, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0C, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0C, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0C, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM
};

static const u8 incall_headphone_buf[] =
{
	// select path route 0x0001
	0x80, 0x26, 0x00, 0x01,

	0x80, 0x0C, 0x0A, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0A, 0x02, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0000: (0 clocks)
	0x80, 0x0C, 0x0A, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0A, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0A, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0A, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0A, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM

	0x80, 0x0C, 0x0C, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0C, 0x02, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0C, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0C, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0C, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM
};

static const u8 incall_bt_buf[] =
{
	// select path route 0x0095
	0x80, 0x26, 0x00, 0x95,

	0x80, 0x0C, 0x0D, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0D, 0x02, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0000: (0 clocks)
	0x80, 0x0C, 0x0D, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0D, 0x04, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0001:TxRising / RxRising
	0x80, 0x0C, 0x0D, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0D, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0D, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM

	0x80, 0x0C, 0x0C, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0C, 0x02, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0C, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0C, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0C, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM
};

static const u8 incall_bt_vpoff_buf[] =
{
	// select path route 0x0095
	0x80, 0x26, 0x00, 0x95,

	0x80, 0x0C, 0x0D, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0D, 0x02, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0000: (0 clocks)
	0x80, 0x0C, 0x0D, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0D, 0x04, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0001:TxRising / RxRising
	0x80, 0x0C, 0x0D, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0D, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0D, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0B: PCM1, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM

	0x80, 0x0C, 0x0C, 0x00, 0x80, 0x0D, 0x00, 0x0F, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x00: PCM WordLength0x800D: SetDeviceParm, 0x000F: 16 Bits
	0x80, 0x0C, 0x0C, 0x02, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x02: PCM DelFromFsTx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x03, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x03: PCM DelFromFsRx0x800D: SetDeviceParm, 0x0001: (1 clocks)
	0x80, 0x0C, 0x0C, 0x04, 0x80, 0x0D, 0x00, 0x02, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x04: PCM Latch Edge0x800D: SetDeviceParm, 0x0002:TxFalling/RxFalling
	0x80, 0x0C, 0x0C, 0x05, 0x80, 0x0D, 0x00, 0x01, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x05: PCM Endianness0x800D: SetDeviceParm, 0x0001:Big Endian
	0x80, 0x0C, 0x0C, 0x06, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x06: PCM Tristate Enable0x800D: SetDeviceParm, 0x0000: Disable
	0x80, 0x0C, 0x0C, 0x07, 0x80, 0x0D, 0x00, 0x00, // ; 0x800C: SetDeviceParmID, 0x0C: PCM2, 0x07: PCM Audio Port Mode0x800D: SetDeviceParm, 0x0000: PCM
};

static const u8 voip_receiver_buf[] =
{
	// select path route 0x0093
	0x80, 0x26, 0x00, 0x93,
};

static const u8 voip_speaker_buf[] =
{
	// select path route 0x0093
	0x80, 0x26, 0x00, 0x93,
};

static const u8 voip_headphone_buf[] =
{
	// select path route 0x0093
	0x80, 0x26, 0x00, 0x93,
};

static const u8 voip_bt_buf[] =
{
	//
};


static const u8 voip_bt_vpoff_buf[] =
{
	//
};

static const u8 suspend_mode[] = {
	0x80, 0x10, 0x00, 0x01
};

static const u8 bypassed_c_2_a[] = {
	/* bypass c to a */
	0x80, 0x52, 0x00, 0xE2,
	0x80, 0x10, 0x00, 0x01
};

#endif /* __LINUX_ES305B_PARAM_H_ */
