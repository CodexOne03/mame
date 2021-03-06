// license:GPL-2.0+
// copyright-holders:Peter Trauner, Wilbert Pol, hap
/******************************************************************************

Driver file to handle emulation of the Novag/Videomaster Chess Champion MK I
Initial version by PeT mess@utanet.at 2000,2001.

The MK I was a clone of Data Cash Systems's CompuChess (1977, one of the first
chess computers). The ROM is identical. DCS sued Novag Industries for copyright
infringement and somehow didn't manage to win the case.

Unlike CompuChess, MK I was a large success, we can assume that it kickstarted
Novag's chess computer generation. It was also distributed as "Computer Chess"
by JS&A, in the same casing as MK I.

To start playing, enter difficulty level (1-6), then when it says "bP", press A
for new game, B for empty board, or C for continue.

MK I hardware description:
- An F8 3850 CPU accompanied by a 3853 memory interface, approx 2MHz
  Variations seen:
  - MOSTEK MK 3853N 7915 Philippines (static memory interface for F8)
  - MOSTEK MK 3850N-3 7917 Philipines (Fairchild F8 CPU)
  - 3850PK F7901 SINGAPORE (Fairchild F8 CPU)
  - 3853PK F7851 SINGAPORE (static memory interface for F8)
- 2KB 2316 compatible ROM
  Variations seen:
  - Signetics 7916E C48091 82S210-1 COPYRIGHT
  - RO-3-8316A 8316A-4480 7904 TAIWAN
- 2 x 2111 256x4 SRAM to provide 256 bytes of RAM
  Variations seen:
  - AM9111 BPC / P2111A-4 7851
- 16 keys placed in a 4 x 4 matrix
- Power on switch
- L/S switch. This switch is directly tied to the RESET pin of the F8 CPU.
  This allows the user to reset the CPU without destroying the RAM contents.
- A 4 character 11 segment digit display using a 15 pin interface. Of the 15
  pins 3 pins are not connected, so three segments are never used and this
  leaves a standard 7 segments display with a dot in the lower right.
- The digit display is driven by two other components:
  - SN75492N MALAYSIA 7840B
  - ULN2033A 7847
- Hardware addressing is controlled by a HBF4001AE.
- No speaker.

******************************************************************************/

#include "emu.h"
#include "cpu/f8/f8.h"
#include "machine/f3853.h"
#include "machine/timer.h"

// internal artwork
#include "cmpchess.lh" // clickable
#include "mk1.lh" // clickable


namespace {

class mk1_state : public driver_device
{
public:
	mk1_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_keypad(*this, "LINE%u", 1U),
		m_delay_display(*this, "delay_display_%u", 0),
		m_out_digit(*this, "digit%u", 0U)
	{ }

	void cmpchess(machine_config &config);
	void mk1(machine_config &config);

	DECLARE_INPUT_CHANGED_MEMBER(reset_switch);

protected:
	virtual void machine_start() override;

private:
	// devices/pointers
	required_device<cpu_device> m_maincpu;
	required_ioport_array<4> m_keypad;
	required_device_array<timer_device, 4> m_delay_display;
	output_finder<4> m_out_digit;

	void main_io(address_map &map);
	void main_map(address_map &map);

	TIMER_DEVICE_CALLBACK_MEMBER(blink) { m_blink = !m_blink; update_display(); }
	TIMER_DEVICE_CALLBACK_MEMBER(delay_display);
	void update_display();

	DECLARE_READ8_MEMBER(input_r);
	DECLARE_WRITE8_MEMBER(digit_select_w);
	DECLARE_WRITE8_MEMBER(digit_data_w);

	u8 m_digit_select;
	u8 m_digit_data;
	bool m_blink;
};

void mk1_state::machine_start()
{
	// resolve handlers
	m_out_digit.resolve();

	// zerofill
	m_digit_select = 0;
	m_digit_data = 0;
	m_blink = false;

	// register for savestates
	save_item(NAME(m_digit_select));
	save_item(NAME(m_digit_data));
	save_item(NAME(m_blink));
}

INPUT_CHANGED_MEMBER(mk1_state::reset_switch)
{
	// reset switch is tied to F3850 RESET pin
	m_maincpu->set_input_line(INPUT_LINE_RESET, newval ? ASSERT_LINE : CLEAR_LINE);

	// clear display
	if (newval)
	{
		m_digit_select = 0xff;
		for (int i = 0; i < 4; i++)
			m_out_digit[i] = 0;
	}
}



/******************************************************************************
    Devices, I/O
******************************************************************************/

// display handling

TIMER_DEVICE_CALLBACK_MEMBER(mk1_state::delay_display)
{
	// clear digits if inactive
	if (BIT(m_digit_select, param))
		m_out_digit[param] = 0;
}

void mk1_state::update_display()
{
	// output digits if active
	for (int i = 0; i < 4; i++)
	{
		if (!BIT(m_digit_select, i))
		{
			// display panel goes into automated blink mode if DP segment is held high
			// and DP segment itself only appears to be active if no other segments are
			u8 mask = (m_digit_data == 1) ? 0x80 : 0x7f;
			mask = (m_blink && m_digit_data & 1) ? 0 : mask;
			m_out_digit[i] = bitswap<8>(m_digit_data,0,2,1,3,4,5,6,7) & mask;
		}
	}
}


// F3850 ports

WRITE8_MEMBER(mk1_state::digit_data_w)
{
	// digit segment data, and also input mux
	m_digit_data = data;
	update_display();
}

WRITE8_MEMBER(mk1_state::digit_select_w)
{
	// d0-d3: digit select (active low)
	// they're strobed, so on rising edge, delay them going off to prevent flicker or stuck display
	for (int i = 0; i < 4; i++)
		if (BIT(~m_digit_select & data, i))
			m_delay_display[i]->adjust(attotime::from_msec(20), i);

	m_digit_select = data;
	update_display();
}

READ8_MEMBER(mk1_state::input_r)
{
	u8 data = m_digit_data;

	// d0-d3: multiplexed inputs from d4-d7
	for (int i = 0; i < 4; i++)
		if (m_digit_data & m_keypad[i]->read())
			data |= 1 << i;

	// d4-d7: multiplexed inputs from d0-d3
	for (int i = 0; i < 4; i++)
		if (BIT(m_digit_data, i))
			data |= m_keypad[i]->read();

	return data;
}



/******************************************************************************
    Address Maps
******************************************************************************/

void mk1_state::main_map(address_map &map)
{
	map(0x0000, 0x07ff).rom();
	map(0x1800, 0x18ff).ram();
}

void mk1_state::main_io(address_map &map)
{
	map(0x00, 0x00).rw(FUNC(mk1_state::input_r), FUNC(mk1_state::digit_data_w));
	map(0x01, 0x01).w(FUNC(mk1_state::digit_select_w));
	map(0x0c, 0x0f).rw("smi", FUNC(f3853_device::read), FUNC(f3853_device::write));
}



/******************************************************************************
    Input Ports
******************************************************************************/

static INPUT_PORTS_START( cmpchess )
	PORT_START("LINE1")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_A) PORT_NAME("A / White King")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_B) PORT_NAME("B / White Queen")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_C) PORT_NAME("C / White Bishop")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_D) PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_NAME("D / Play")

	PORT_START("LINE2")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_E) PORT_NAME("E / White Knight")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_F) PORT_NAME("F / White Rook")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_G) PORT_NAME("G / White Pawn")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_H) PORT_CODE(KEYCODE_M) PORT_NAME("H / md") // more data

	PORT_START("LINE3")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD) PORT_NAME("1 / Black King")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD) PORT_NAME("2 / Black Queen")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD) PORT_NAME("3 / Black Bishop")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD) PORT_NAME("4 / fp") // find piece(position)

	PORT_START("LINE4")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD) PORT_NAME("5 / Black Knight")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD) PORT_NAME("6 / Black Rook")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD) PORT_NAME("7 / Black Pawn")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD) PORT_NAME("8 / ep") // enter piece(position)

	PORT_START("RESET")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_CODE(KEYCODE_R) PORT_TOGGLE PORT_CHANGED_MEMBER(DEVICE_SELF, mk1_state, reset_switch, nullptr) PORT_NAME("Reset Switch") // L.S. switch on the MK I
INPUT_PORTS_END



/******************************************************************************
    Machine Configs
******************************************************************************/

void mk1_state::cmpchess(machine_config &config)
{
	/* basic machine hardware */
	F8(config, m_maincpu, 3.579545_MHz_XTAL/2); // Fairchild 3850PK
	m_maincpu->set_addrmap(AS_PROGRAM, &mk1_state::main_map);
	m_maincpu->set_addrmap(AS_IO, &mk1_state::main_io);
	m_maincpu->set_irq_acknowledge_callback("smi", FUNC(f3853_device::int_acknowledge));

	f3853_device &smi(F3853(config, "smi", 3.579545_MHz_XTAL/2));
	smi.int_req_callback().set_inputline("maincpu", F8_INPUT_LINE_INT_REQ);

	/* video hardware */
	for (int i = 0; i < 4; i++)
		TIMER(config, m_delay_display[i]).configure_generic(FUNC(mk1_state::delay_display));

	TIMER(config, "blink_display").configure_periodic(FUNC(mk1_state::blink), attotime::from_msec(250)); // approximation
	config.set_default_layout(layout_cmpchess);
}

void mk1_state::mk1(machine_config &config)
{
	cmpchess(config);

	/* basic machine hardware */
	m_maincpu->set_clock(2000000); // it's a bit faster than cmpchess
	subdevice<f3853_device>("smi")->set_clock(2000000);

	config.set_default_layout(layout_mk1);
}



/******************************************************************************
    ROM Definitions
******************************************************************************/

ROM_START( cmpchess )
	ROM_REGION( 0x0800, "maincpu", 0 )
	ROM_LOAD("32014-4950", 0x0000, 0x0800, CRC(278f7bf3) SHA1(b384c95ba691d52dfdddd35987a71e9746a46170) )
ROM_END

ROM_START( ccmk1 )
	ROM_REGION( 0x0800, "maincpu", 0 )
	ROM_LOAD("82c210-1", 0x0000, 0x0800, CRC(278f7bf3) SHA1(b384c95ba691d52dfdddd35987a71e9746a46170) )
ROM_END

} // anonymous namespace



/******************************************************************************
    Drivers
******************************************************************************/

//    YEAR  NAME      PARENT   CMP MACHINE   INPUT     STATE      INIT        COMPANY, FULLNAME, FLAGS
CONS( 1977, cmpchess, 0,        0, cmpchess, cmpchess, mk1_state, empty_init, "Data Cash Systems", "CompuChess", MACHINE_NO_SOUND_HW | MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
CONS( 1978, ccmk1,    cmpchess, 0, mk1,      cmpchess, mk1_state, empty_init, "Novag", "Chess Champion: MK I", MACHINE_NO_SOUND_HW | MACHINE_SUPPORTS_SAVE | MACHINE_CLICKABLE_ARTWORK )
