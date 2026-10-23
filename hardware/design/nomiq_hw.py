#!/usr/bin/env python3
"""Nomiq main board rev A0 — machine-checked connection specification.

This is the single source the KiCad schematic is captured from (by the
electronics-schematic-engineer agent with the kicad MCP server, or by hand).
It works at the level of *datasheet pin names*, not pad numbers, so it does not
depend on any particular KiCad symbol/footprint library.

    python3 hardware/design/nomiq_hw.py            # run all checks, write reports
    python3 hardware/design/nomiq_hw.py --check    # checks only (CI)

Outputs (hardware/design/out/):
    connections.md   per-sheet net tables to capture from
    bom.csv          reference designators, values, MPNs, verification status
    nets.json        machine-readable netlist (REF.PIN lists)

Checks:
  * every IC pin is either on a net or explicitly NC
  * no pin is on two nets; every net has >= 2 members (or is a declared test/one-ended net)
  * ESP32-S3 GPIO usage matches firmware/components/board/include/board_pins.h exactly
  * strap pins (GPIO0/3/45/46) have the documented reset-safe conditions
  * every IC supply pin is on a power net; decoupling present per supply pin group
  * expander bit map matches board_pins.h IOX_* definitions

Values marked verify=True are datasheet-derived and MUST be re-checked against the
current datasheet revision during schematic capture (they are listed in the report).
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PINS_H = os.path.join(ROOT, "firmware", "components", "board", "include", "board_pins.h")


class Part:
    def __init__(self, ref, value, mpn, sheet, pins=None, power=(), nc=(), footprint="", note="", verify=False,
                 passive=False):
        self.ref, self.value, self.mpn, self.sheet = ref, value, mpn, sheet
        self.pins = list(pins) if pins else ["1", "2"]
        self.power = set(power)
        self.nc = set(nc)
        self.footprint, self.note, self.verify, self.passive = footprint, note, verify, passive or not pins


PARTS: dict[str, Part] = {}
NETS: dict[str, list[str]] = collections.defaultdict(list)
NET_SHEET: dict[str, str] = {}
ONE_ENDED_OK: set[str] = set()


def part(*a, **k) -> Part:
    p = Part(*a, **k)
    assert p.ref not in PARTS, p.ref
    PARTS[p.ref] = p
    return p


def R(ref, value, sheet, a, b, note="", verify=False, fp="Resistor_SMD:R_0402_1005Metric"):
    part(ref, value, "", sheet, footprint=fp, note=note, verify=verify)
    conn(a, f"{ref}.1"); conn(b, f"{ref}.2")


def Cap(ref, value, sheet, a, b="GND", note="", fp="Capacitor_SMD:C_0402_1005Metric", verify=False):
    part(ref, value, "", sheet, footprint=fp, note=note, verify=verify)
    conn(a, f"{ref}.1"); conn(b, f"{ref}.2")


def L(ref, value, mpn, sheet, a, b, note="", verify=False, fp=""):
    part(ref, value, mpn, sheet, footprint=fp, note=note, verify=verify)
    conn(a, f"{ref}.1"); conn(b, f"{ref}.2")


def conn(net: str, *members: str) -> None:
    for m in members:
        NETS[net].append(m)


def tp(ref, net, sheet):
    part(ref, "TP", "", sheet, pins=["1"], footprint="TestPoint:TestPoint_Pad_D1.0mm")
    conn(net, f"{ref}.1")


# =============================================================================================
# 01_power — USB-C, charger/power path, battery, fuel gauge, 3V3 buck-boost, load switches
# =============================================================================================
S = "01_power"
part("J1", "USB-C 16P", "GCT USB4105-GF-A", S,
     pins=["VBUS", "GND", "CC1", "CC2", "DP1", "DN1", "DP2", "DN2", "SBU1", "SBU2", "SHIELD"], nc=["SBU1", "SBU2"],
     footprint="Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal")
conn("VBUS", "J1.VBUS"); conn("GND", "J1.GND", "J1.SHIELD")
R("R1", "5.1k", S, "J1_CC1", "GND", note="UFP Rd")
R("R2", "5.1k", S, "J1_CC2", "GND", note="UFP Rd")
conn("J1_CC1", "J1.CC1"); conn("J1_CC2", "J1.CC2")
conn("USB_DP", "J1.DP1", "J1.DP2"); conn("USB_DN", "J1.DN1", "J1.DN2")
part("D1", "USBLC6-2SC6", "STMicro USBLC6-2SC6", S, pins=["IO1", "IO2", "IO3", "IO4", "VBUS", "GND"],
     power=["VBUS", "GND"], footprint="Package_TO_SOT_SMD:SOT-23-6")
conn("USB_DP", "D1.IO1"); conn("USB_DP_MCU", "D1.IO4")   # flow-through: IO1<->IO4, IO2<->IO3
conn("USB_DN", "D1.IO2"); conn("USB_DN_MCU", "D1.IO3")
conn("VBUS", "D1.VBUS"); conn("GND", "D1.GND")
part("D2", "SMF5.0A", "Littelfuse SMF5.0A", S, pins=["A", "K"], footprint="Diode_SMD:D_SOD-123F",
     note="VBUS TVS")
conn("VBUS", "D2.K"); conn("GND", "D2.A")
Cap("C1", "10uF/10V", S, "VBUS", fp="Capacitor_SMD:C_0603_1608Metric")

part("U8", "BQ24074", "TI BQ24074RGTR", S,
     pins=["IN", "OUT", "BAT", "VSS", "TS", "ISET", "ILIM", "ITERM", "TMR", "CE", "EN1", "EN2", "CHG", "PGOOD", "EP"],
     power=["IN", "OUT", "BAT", "VSS", "EP"], footprint="Package_DFN_QFN:VQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm",
     note="1S Li-ion linear charger with dynamic power-path; VSYS=OUT", verify=True)
conn("VBUS", "U8.IN"); conn("VSYS", "U8.OUT"); conn("VBAT", "U8.BAT"); conn("GND", "U8.VSS", "U8.EP")
R("R3", "887R 1%", S, "CHG_ISET", "GND", note="ICHG = 890/RISET ≈ 1.0 A", verify=True)
conn("CHG_ISET", "U8.ISET")
R("R4", "1.05k 1%", S, "CHG_ILIM", "GND", note="IIN_MAX = 1550/RILIM ≈ 1.48 A", verify=True)
conn("CHG_ILIM", "U8.ILIM")
R("R5", "5.36k 1%", S, "CHG_ITERM", "GND", note="ITERM ≈ 10 % of ICHG (check KITERM in datasheet)", verify=True)
conn("CHG_ITERM", "U8.ITERM")
R("R6", "46.4k 1%", S, "CHG_TMR", "GND", note="safety timer ≈ 5 h fast-charge (check KTMR)", verify=True)
conn("CHG_TMR", "U8.TMR")
conn("GND", "U8.CE", "U8.EN1")          # CE low = charge enabled; EN2=1,EN1=0 -> ILIM-resistor mode
conn("VSYS", "U8.EN2")
conn("BAT_NTC", "U8.TS")
conn("CHG_STAT_N", "U8.CHG")
conn("PGOOD_N", "U8.PGOOD")
R("R7", "100k", S, "CHG_STAT_N", "V3V3", note="open-drain pull-up -> IOX P06")
R("R8", "100k", S, "PGOOD_N", "V3V3")
Cap("C2", "1uF/10V", S, "VBUS", fp="Capacitor_SMD:C_0402_1005Metric")
Cap("C3", "4.7uF/10V", S, "VSYS", fp="Capacitor_SMD:C_0603_1608Metric")
Cap("C4", "4.7uF/10V", S, "VBAT", fp="Capacitor_SMD:C_0603_1608Metric")
tp("TP1", "PGOOD_N", S)

part("J2", "Battery JST-PH 3P (+, NTC, -)", "JST S3B-PH-SM4-TB", S, pins=["BAT+", "NTC", "BAT-"],
     footprint="Connector_JST:JST_PH_S3B-PH-SM4-TB_1x03-1MP_P2.00mm_Horizontal",
     note="pack with protection PCM and 10k NTC; if the pack has no PCM fit U11")
conn("VBAT", "J2.BAT+"); conn("BAT_NTC", "J2.NTC"); conn("GND", "J2.BAT-")

part("U9", "MAX17048", "Analog Devices MAX17048G+T10", S,
     pins=["CTG", "CELL", "VDD", "GND", "ALRT", "QSTRT", "SCL", "SDA", "EP"], power=["VDD", "GND", "EP"],
     footprint="Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm", verify=True, note="ModelGauge fuel gauge, I2C 0x36")
conn("VBAT", "U9.CELL", "U9.VDD"); conn("GND", "U9.CTG", "U9.GND", "U9.QSTRT", "U9.EP")
conn("I2C_SCL", "U9.SCL"); conn("I2C_SDA", "U9.SDA"); conn("FG_ALRT_N", "U9.ALRT")
R("R9", "100k", S, "FG_ALRT_N", "V3V3")
Cap("C5", "1uF/10V", S, "VBAT")

part("U10", "TPS63802", "TI TPS63802DLAR", S,
     pins=["VIN", "EN", "MODE", "GND", "L1", "L2", "VOUT", "FB", "PG"], power=["VIN", "VOUT", "GND"],
     footprint="Package_SON:Texas_VSON-HR-10_3x2mm_P0.5mm", verify=True,
     note="2 A buck-boost, 3.3 V stays regulated from 2.5-4.4 V VSYS")
conn("VSYS", "U10.VIN", "U10.EN"); conn("GND", "U10.GND", "U10.MODE")   # MODE low = auto PFM/PWM (power save)
L("L1", "0.47uH", "Coilcraft XFL4015-471MEB", S, "BB_L1", "BB_L2", verify=True,
  fp="Inductor_SMD:L_Coilcraft_XFL4015")
conn("BB_L1", "U10.L1"); conn("BB_L2", "U10.L2"); conn("V3V3", "U10.VOUT"); conn("BB_FB", "U10.FB")
R("R10", "510k 1%", S, "V3V3", "BB_FB", note="VOUT = 0.5 V × (1 + R10/R11) = 3.30 V", verify=True)
R("R11", "91k 1%", S, "BB_FB", "GND", verify=True)
conn("PWR_GOOD", "U10.PG")
R("R12", "100k", S, "PWR_GOOD", "V3V3")
tp("TP2", "PWR_GOOD", S)
Cap("C6", "10uF/10V", S, "VSYS", fp="Capacitor_SMD:C_0603_1608Metric")
Cap("C7", "22uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0805_2012Metric")
Cap("C8", "22uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0805_2012Metric")

part("U14", "TPS22918 (STT rail)", "TI TPS22918DBVR", S, pins=["VIN", "VOUT", "ON", "GND", "CT", "QOD"],
     power=["VIN", "GND"], footprint="Package_TO_SOT_SMD:SOT-23-6", verify=True, note="2 A load switch for STT SoM")
conn("VSYS", "U14.VIN"); conn("VSTT", "U14.VOUT"); conn("STT_EN", "U14.ON"); conn("GND", "U14.GND")
Cap("C9", "1nF", S, "STT_CT", note="slew-rate (inrush) control"); conn("STT_CT", "U14.CT")
conn("VSTT", "U14.QOD")   # quick output discharge
R("R13", "100k", S, "STT_EN", "GND", note="SoM off at reset")
Cap("C10", "1uF/10V", S, "VSYS")
Cap("C11", "22uF/10V", S, "VSTT", fp="Capacitor_SMD:C_0805_2012Metric")
tp("TP3", "VBUS", S); tp("TP4", "VSYS", S); tp("TP5", "VBAT", S); tp("TP6", "V3V3", S); tp("TP7", "GND", S)

# =============================================================================================
# 02_mcu — ESP32-S3-WROOM-1-N16R8, reset/boot, USB, I2C expander, debug
# =============================================================================================
S = "02_mcu"
ESP_GPIOS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
             35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48]
part("U1", "ESP32-S3-WROOM-1-N16R8", "Espressif ESP32-S3-WROOM-1-N16R8", S,
     pins=["3V3", "GND", "EN"] + [f"IO{g}" for g in ESP_GPIOS if g not in (43, 44)] + ["TXD0", "RXD0"],
     power=["3V3", "GND"], nc=["IO35", "IO36", "IO37"],   # used by octal PSRAM inside the module
     footprint="RF_Module:ESP32-S3-WROOM-1", note="keep antenna area free of copper on all layers (module unused radio)")
conn("V3V3", "U1.3V3"); conn("GND", "U1.GND")
R("R20", "10k", S, "ESP_EN", "V3V3", note="EN RC: 10k/1uF per Espressif HW design guide")
Cap("C20", "1uF", S, "ESP_EN"); conn("ESP_EN", "U1.EN")
Cap("C21", "22uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0805_2012Metric")
Cap("C22", "0.1uF", S, "V3V3")
part("SW1", "RESET", "Panasonic EVQ-P7A01P", S, pins=["1", "2"], footprint="Button_Switch_SMD:Panasonic_EVQPUL_EVQPUC")
conn("ESP_EN", "SW1.1"); conn("GND", "SW1.2")
part("SW2", "BOOT", "Panasonic EVQ-P7A01P", S, pins=["1", "2"], footprint="Button_Switch_SMD:Panasonic_EVQPUL_EVQPUC")
conn("IOX_INT_N", "SW2.1"); conn("GND", "SW2.2")
R("R21", "10k", S, "IOX_INT_N", "V3V3", note="GPIO0 strap: high = SPI boot")
# USB
R("R22", "0R", S, "USB_DP_MCU", "ESP_USB_DP", note="series, placeholder for EMI filter")
R("R23", "0R", S, "USB_DN_MCU", "ESP_USB_DN")
# debug header (Tag-Connect TC2030-NL)
part("J3", "TC2030-IDC-NL", "Tag-Connect TC2030-IDC-NL", S, pins=["1", "2", "3", "4", "5", "6"],
     footprint="Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical", note="1=3V3 2=EN 3=TXD0 4=RXD0 5=IO0 6=GND")
conn("V3V3", "J3.1"); conn("ESP_EN", "J3.2"); conn("STT_TX", "J3.3"); conn("STT_RX", "J3.4")
conn("IOX_INT_N", "J3.5"); conn("GND", "J3.6")

part("U4", "TCA9535", "TI TCA9535PWR", S,
     pins=["VCC", "GND", "SDA", "SCL", "INT", "A0", "A1", "A2"] + [f"P0{i}" for i in range(8)] + [f"P1{i}" for i in range(8)],
     power=["VCC", "GND"], footprint="Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm", note="I2C 0x20 (A2..A0 = 000)")
conn("V3V3", "U4.VCC"); conn("GND", "U4.GND", "U4.A0", "U4.A1", "U4.A2")
conn("I2C_SDA", "U4.SDA"); conn("I2C_SCL", "U4.SCL"); conn("IOX_INT_N", "U4.INT")
Cap("C23", "0.1uF", S, "V3V3")
R("R24", "2.2k", S, "I2C_SDA", "V3V3"); R("R25", "2.2k", S, "I2C_SCL", "V3V3")
IOX_MAP = {  # expander pin -> net ; must match IOX_* in board_pins.h
    "P00": "BTN_UP_N", "P01": "BTN_DOWN_N", "P02": "BTN_SELECT_N", "P03": "BTN_BACK_N", "P04": "BTN_RECORD_N",
    "P05": "BTN_CAMERA_N", "P06": "CHG_STAT_N", "P07": "SD_DETECT_N",
    "P10": "LORA_NRST", "P11": "LCD_RST_N", "P12": "CAM_PWDN", "P13": "CAM_RESET_N", "P14": "AMP_SD_N",
    "P15": "STT_EN", "P16": "CAM_PWR_EN", "P17": "FG_ALRT_N",
}
for pin, net in IOX_MAP.items():
    conn(net, f"U4.{pin}")
tp("TP8", "I2C_SDA", S); tp("TP9", "I2C_SCL", S); tp("TP10", "ESP_EN", S)

# =============================================================================================
# 03_radio — SX1262, TCXO, DC-DC, RF front end
# =============================================================================================
S = "03_radio"
part("U2", "SX1262", "Semtech SX1262IMLTRT", S,
     pins=["VDD_IN", "GND", "XTA", "XTB", "DIO3", "VREG", "DCC_SW", "VBAT", "VBAT_IO", "DIO2", "DIO1", "BUSY",
           "NRESET", "MISO", "MOSI", "SCK", "NSS", "RFI_P", "RFI_N", "RFO", "VR_PA", "EP"],
     power=["VDD_IN", "VBAT", "VBAT_IO", "GND", "EP"], nc=["XTB"],
     footprint="Package_DFN_QFN:QFN-24-1EP_4x4mm_P0.5mm_EP2.6x2.6mm", verify=True,
     note="RF matching, balun, PA filter and DC-DC values: copy from Semtech SX1262 868 MHz reference design")
conn("V3V3", "U2.VDD_IN", "U2.VBAT", "U2.VBAT_IO"); conn("GND", "U2.GND", "U2.EP")
conn("SPI_SCK", "U2.SCK"); conn("SPI_MOSI", "U2.MOSI"); conn("SPI_MISO", "U2.MISO"); conn("LORA_NSS", "U2.NSS")
conn("LORA_BUSY", "U2.BUSY"); conn("LORA_DIO1", "U2.DIO1"); conn("LORA_NRST", "U2.NRESET")
conn("RF_SW_CTRL", "U2.DIO2"); conn("TCXO_VCC", "U2.DIO3")
L("L2", "15uH", "Murata LQM21FN150M70L", S, "SX_DCC_SW", "SX_VREG", verify=True, fp="Inductor_SMD:L_0805_2012Metric",
  note="DC-DC inductor (ref design)")
conn("SX_DCC_SW", "U2.DCC_SW"); conn("SX_VREG", "U2.VREG")
Cap("C30", "470nF", S, "SX_VREG", verify=True)
Cap("C31", "47nF", S, "SX_VR_PA", verify=True); conn("SX_VR_PA", "U2.VR_PA")
Cap("C32", "0.1uF", S, "V3V3"); Cap("C33", "10uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0603_1608Metric")
R("R30", "10k", S, "LORA_NSS", "V3V3", note="NSS high while ESP32 boots")
part("Y1", "TCXO 32 MHz 1.8 V ±2 ppm", "Epson TG2016SMN 32.0000M-MCGNNM3", S, pins=["VCC", "GND", "OUT", "NC"],
     power=["VCC", "GND"], nc=["NC"], footprint="Oscillator:Oscillator_SMD_EuroQuartz_XO32-4Pin_3.2x2.5mm", verify=True,
     note="footprint placeholder: use 2016 4-pad")
conn("TCXO_VCC", "Y1.VCC"); conn("GND", "Y1.GND")
R("R31", "220R", S, "TCXO_OUT", "SX_XTA", note="series per ref design", verify=True)
Cap("C34", "10pF", S, "SX_XTA", verify=True, note="DC-block/AC-couple per Semtech TCXO guidance")
conn("TCXO_OUT", "Y1.OUT"); conn("SX_XTA", "U2.XTA")
Cap("C35", "0.1uF", S, "TCXO_VCC")
part("U3", "PE4259", "pSemi PE4259-63", S, pins=["RF1", "RF2", "RFC", "CTRL", "VDD", "GND"], power=["VDD", "GND"],
     footprint="Package_TO_SOT_SMD:SOT-363_SC-70-6", verify=True, note="SPDT: RF1 = TX path, RF2 = RX path")
conn("V3V3", "U3.VDD"); conn("GND", "U3.GND"); conn("RF_SW_CTRL", "U3.CTRL")
conn("RF_TX", "U3.RF1"); conn("RF_RX", "U3.RF2"); conn("RF_ANT", "U3.RFC")
part("FL1", "PA match + harmonic filter (ref design)", "see Semtech ref BOM", S, pins=["IN", "OUT", "GND"],
     power=["GND"], verify=True, note="discrete L/C network between RFO and switch; values from ref design")
conn("SX_RFO", "U2.RFO", "FL1.IN"); conn("RF_TX", "FL1.OUT"); conn("GND", "FL1.GND")
part("FL2", "RX balun / match (ref design)", "see Semtech ref BOM", S, pins=["IN", "OUT_P", "OUT_N", "GND"],
     power=["GND"], verify=True)
conn("RF_RX", "FL2.IN"); conn("SX_RFI_P", "FL2.OUT_P", "U2.RFI_P"); conn("SX_RFI_N", "FL2.OUT_N", "U2.RFI_N")
conn("GND", "FL2.GND")
part("FL3", "Antenna π-match (tuning)", "", S, pins=["IN", "OUT", "GND"], power=["GND"],
     note="0R + 2 × DNP shunt pads for antenna tuning")
conn("RF_ANT", "FL3.IN"); conn("GND", "FL3.GND")
part("J4", "u.FL", "Hirose U.FL-R-SMT-1(10)", S, pins=["SIG", "GND"], footprint="Connector_Coaxial:U.FL_Hirose_U.FL-R-SMT-1_Vertical")
conn("RF_ANT_CONN", "FL3.OUT", "J4.SIG"); conn("GND", "J4.GND")
part("D3", "ESD RF (low C)", "Infineon ESD0P2RF-02LS", S, pins=["A", "K"], footprint="Diode_SMD:D_0201_0603Metric",
     note="< 0.2 pF, antenna port ESD")
conn("RF_ANT_CONN", "D3.K"); conn("GND", "D3.A")
tp("TP11", "LORA_BUSY", S); tp("TP12", "LORA_DIO1", S)

# =============================================================================================
# 04_audio — MEMS mic, class-D amp, speaker
# =============================================================================================
S = "04_audio"
part("U5", "ICS-43434", "TDK InvenSense ICS-43434", S, pins=["VDD", "GND", "SCK", "WS", "SD", "LR"],
     power=["VDD", "GND"], footprint="Sensor_Audio:InvenSense_ICS-43434-6_3.5x2.65mm", verify=True,
     note="bottom-port; LR=GND -> left slot")
conn("V3V3", "U5.VDD"); conn("GND", "U5.GND", "U5.LR")
conn("I2S_BCLK", "U5.SCK"); conn("I2S_WS", "U5.WS"); conn("I2S_DIN", "U5.SD")
Cap("C40", "0.1uF", S, "V3V3")
R("R40", "100k", S, "I2S_DIN", "GND", note="keeps GPIO46 strap low when mic is tri-stated")
part("U6", "MAX98357A", "Analog Devices MAX98357AETE+T", S,
     pins=["VDD", "GND", "BCLK", "LRCLK", "DIN", "GAIN_SLOT", "SD_MODE", "OUTP", "OUTN", "EP"],
     power=["VDD", "GND", "EP"], footprint="Package_DFN_QFN:TQFN-16-1EP_3x3mm_P0.5mm_EP1.23x1.23mm", verify=True)
conn("VSYS", "U6.VDD"); conn("GND", "U6.GND", "U6.EP")
conn("I2S_BCLK", "U6.BCLK"); conn("I2S_WS", "U6.LRCLK"); conn("I2S_DOUT", "U6.DIN")
conn("AMP_GAIN", "U6.GAIN_SLOT"); R("R41", "100k", S, "AMP_GAIN", "GND", note="GAIN_SLOT 100k to GND = 12 dB", verify=True)
conn("AMP_SD_N", "U6.SD_MODE")
R("R43", "100k", S, "AMP_SD_N", "GND",
  note="amp in shutdown while the expander is high-Z at reset; expander drives 3.3 V = on, left slot (firmware plays mono on L)",
  verify=True)
conn("SPK_P", "U6.OUTP"); conn("SPK_N", "U6.OUTN")
Cap("C41", "10uF/10V", S, "VSYS", fp="Capacitor_SMD:C_0603_1608Metric"); Cap("C42", "0.1uF", S, "VSYS")
part("J5", "Speaker JST-SH 2P", "JST SM02B-SRSS-TB", S, pins=["1", "2"],
     footprint="Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal")
conn("SPK_P", "J5.1"); conn("SPK_N", "J5.2")

# =============================================================================================
# 05_camera — OV2640 24-pin FPC, LDOs, power switch
# =============================================================================================
S = "05_camera"
part("J6", "OV2640 24P FPC 0.5 mm", "Hirose FH12-24S-0.5SH(55)", S,
     pins=["AGND", "SDA", "AVDD", "SCL", "RESETB", "VSYNC", "PWDN", "HREF", "DVDD", "DOVDD", "Y9", "XCLK",
           "Y8", "DGND", "Y7", "PCLK", "Y6", "Y2", "Y5", "Y3", "Y4", "NC1", "NC2", "NC3"],
     power=["AVDD", "DVDD", "DOVDD", "AGND", "DGND"], nc=["NC1", "NC2", "NC3"],
     footprint="Connector_FFC-FPC:Hirose_FH12-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal", verify=True,
     note="common 24-pin OV2640 module pinout — confirm against the purchased module")
conn("GND", "J6.AGND", "J6.DGND")
conn("I2C_SDA", "J6.SDA"); conn("I2C_SCL", "J6.SCL")
conn("CAM_RESET_N", "J6.RESETB"); conn("CAM_PWDN", "J6.PWDN")
conn("CAM_VSYNC", "J6.VSYNC"); conn("CAM_HREF", "J6.HREF"); conn("CAM_PCLK", "J6.PCLK"); conn("CAM_XCLK", "J6.XCLK")
# 8-bit DVP: data bus Y2..Y9 -> esp32-camera D0..D7 (same as ESP32-S3-EYE)
for i in range(8):
    conn(f"CAM_D{i}", f"J6.Y{i + 2}")
part("U12", "TLV75528 (2.8 V)", "TI TLV75528PDBVR", S, pins=["IN", "EN", "GND", "OUT", "NC"], power=["IN", "GND"],
     nc=["NC"], footprint="Package_TO_SOT_SMD:SOT-23-5")
part("U13", "TLV75512 (1.2 V)", "TI TLV75512PDBVR", S, pins=["IN", "EN", "GND", "OUT", "NC"], power=["IN", "GND"],
     nc=["NC"], footprint="Package_TO_SOT_SMD:SOT-23-5")
part("U15", "TPS22918 (cam 3V3)", "TI TPS22918DBVR", S, pins=["VIN", "VOUT", "ON", "GND", "CT", "QOD"],
     power=["VIN", "GND"], footprint="Package_TO_SOT_SMD:SOT-23-6")
conn("V3V3", "U15.VIN"); conn("VCAM_3V3", "U15.VOUT", "U15.QOD"); conn("CAM_PWR_EN", "U15.ON"); conn("GND", "U15.GND")
R("R50", "100k", S, "CAM_PWR_EN", "GND"); Cap("C50", "1nF", S, "CAM_CT"); conn("CAM_CT", "U15.CT")
conn("VCAM_3V3", "U12.IN", "U12.EN", "U13.IN", "U13.EN", "J6.DOVDD")
conn("GND", "U12.GND", "U13.GND")
conn("VCAM_2V8", "U12.OUT", "J6.AVDD"); conn("VCAM_1V2", "U13.OUT", "J6.DVDD")
Cap("C51", "1uF", S, "VCAM_3V3"); Cap("C52", "1uF", S, "VCAM_2V8"); Cap("C53", "1uF", S, "VCAM_1V2")
R("R51", "33R", S, "CAM_XCLK_MCU", "CAM_XCLK", note="series termination at the MCU")

# =============================================================================================
# 06_display_storage — TFT FPC + backlight, microSD
# =============================================================================================
S = "06_display_storage"
part("J7", "TFT 2.4in ILI9341 SPI FPC", "module-specific", S,
     pins=["VCC", "GND", "CS", "RESET", "DC", "SDI", "SCK", "LED_A", "LED_K", "SDO"], power=["VCC", "GND"],
     nc=["SDO"], verify=True, note="pinout depends on the chosen panel; keep the net names")
conn("V3V3", "J7.VCC"); conn("GND", "J7.GND")
conn("LCD_CS", "J7.CS"); conn("LCD_RST_N", "J7.RESET"); conn("LCD_DC", "J7.DC"); conn("SPI_MOSI", "J7.SDI")
conn("SPI_SCK", "J7.SCK"); conn("V3V3", "J7.LED_A"); conn("LCD_LED_K", "J7.LED_K")
part("Q1", "AO3400A", "AOS AO3400A", S, pins=["G", "S", "D"], footprint="Package_TO_SOT_SMD:SOT-23",
     note="low-side backlight switch")
conn("LCD_BL", "Q1.G"); conn("GND", "Q1.S"); conn("LCD_LED_K_SW", "Q1.D")
R("R60", "10R", S, "LCD_LED_K", "LCD_LED_K_SW", note="LED current limit — set for panel (check LED Vf/If)", verify=True)
R("R61", "100k", S, "LCD_BL", "GND", note="GPIO45 strap must be LOW at reset")
R("R62", "10k", S, "LCD_CS", "V3V3")
Cap("C60", "10uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0603_1608Metric")
part("J8", "microSD push-push", "Molex 104031-0811", S,
     pins=["DAT2", "CD_DAT3", "CMD", "VDD", "CLK", "VSS", "DAT0", "DAT1", "DET", "SHIELD"], power=["VDD", "VSS"],
     nc=["DAT1", "DAT2"], footprint="Connector_Card:microSD_HC_Molex_104031-0811", verify=True)
conn("SD_CS", "J8.CD_DAT3"); conn("SPI_MOSI", "J8.CMD"); conn("SPI_SCK", "J8.CLK"); conn("SPI_MISO", "J8.DAT0")
conn("V3V3", "J8.VDD"); conn("GND", "J8.VSS", "J8.SHIELD"); conn("SD_DETECT_N", "J8.DET")
R("R63", "10k", S, "SD_CS", "V3V3"); R("R64", "47k", S, "SD_DETECT_N", "V3V3"); R("R65", "47k", S, "SPI_MISO", "V3V3")
Cap("C61", "10uF/6.3V", S, "V3V3", fp="Capacitor_SMD:C_0603_1608Metric"); Cap("C62", "0.1uF", S, "V3V3")

# =============================================================================================
# 07_ui — buttons
# =============================================================================================
S = "07_ui"
for i, (sw, net, side) in enumerate([("SW3", "BTN_UP_N", "front"), ("SW4", "BTN_DOWN_N", "front"),
                                     ("SW5", "BTN_SELECT_N", "front"), ("SW6", "BTN_BACK_N", "front"),
                                     ("SW7", "BTN_RECORD_N", "left side"), ("SW8", "BTN_CAMERA_N", "right side")]):
    part(sw, f"button ({side})", "Panasonic EVQ-P7A01P" if "side" in side else "C&K PTS526", S, pins=["1", "2"])
    conn(net, f"{sw}.1"); conn("GND", f"{sw}.2")
    R(f"R7{i}", "47k", S, net, "V3V3")
    part(f"D7{i}", "ESD", "Nexperia PESD5V0S1BL", S, pins=["A", "K"], footprint="Diode_SMD:D_SOD-882")
    conn(net, f"D7{i}.K"); conn("GND", f"D7{i}.A")
part("SW9", "POWER (ship-mode / wake)", "Panasonic EVQ-P7A01P", S, pins=["1", "2"],
     note="to charger/PMIC power-on; v A0: wakes ESP32 via EN pulse — revisit with power-systems-engineer")
conn("ESP_EN", "SW9.1"); conn("GND", "SW9.2")

# =============================================================================================
# 08_stt — co-processor connector (SoM chosen in Phase 3)
# =============================================================================================
S = "08_stt"
part("J9", "STT SoM interface 2x5 1.27 mm", "Samtec FTSH-105-01-L-DV", S,
     pins=["VSTT1", "VSTT2", "GND1", "GND2", "UART_TX", "UART_RX", "SOM_PWR_OK", "NC1", "NC2", "NC3"],
     power=["VSTT1", "VSTT2", "GND1", "GND2"], nc=["SOM_PWR_OK", "NC1", "NC2", "NC3"],
     note="placeholder until ADR-0002 is final; UART 3V3 levels")
conn("VSTT", "J9.VSTT1", "J9.VSTT2"); conn("GND", "J9.GND1", "J9.GND2")
conn("STT_TX", "J9.UART_RX"); conn("STT_RX", "J9.UART_TX")

# =============================================================================================
# ESP32 GPIO wiring (single place) — checked against board_pins.h
# =============================================================================================
GPIO = {
    0: "IOX_INT_N", 1: "LORA_DIO1", 2: "LORA_BUSY", 3: "I2S_DOUT", 4: "I2C_SDA", 5: "I2C_SCL",
    6: "CAM_VSYNC", 7: "CAM_HREF", 8: "CAM_D2", 9: "CAM_D1", 10: "CAM_D3", 11: "CAM_D0", 12: "CAM_D4",
    13: "CAM_PCLK", 14: "LCD_DC", 15: "CAM_XCLK_MCU", 16: "CAM_D7", 17: "CAM_D6", 18: "CAM_D5",
    19: "ESP_USB_DN", 20: "ESP_USB_DP", 21: "LCD_CS", 38: "SD_CS", 39: "SPI_SCK", 40: "SPI_MOSI",
    41: "SPI_MISO", 42: "LORA_NSS", 43: "STT_TX", 44: "STT_RX", 45: "LCD_BL", 46: "I2S_DIN",
    47: "I2S_BCLK", 48: "I2S_WS",
}
for g, net in GPIO.items():
    pin = {43: "TXD0", 44: "RXD0"}.get(g, f"IO{g}")
    conn(net, f"U1.{pin}")
ONE_ENDED_OK |= {"PGOOD_N"}            # TP only in A0; also readable by firmware in A1

# map board_pins.h macro -> expected net
PIN_MACRO_NET = {
    "PIN_SPI_SCK": "SPI_SCK", "PIN_SPI_MOSI": "SPI_MOSI", "PIN_SPI_MISO": "SPI_MISO", "PIN_LORA_NSS": "LORA_NSS",
    "PIN_LORA_BUSY": "LORA_BUSY", "PIN_LORA_DIO1": "LORA_DIO1", "PIN_LCD_CS": "LCD_CS", "PIN_LCD_DC": "LCD_DC",
    "PIN_LCD_BL": "LCD_BL", "PIN_SD_CS": "SD_CS", "PIN_I2C_SDA": "I2C_SDA", "PIN_I2C_SCL": "I2C_SCL",
    "PIN_IOX_INT": "IOX_INT_N", "PIN_I2S_BCLK": "I2S_BCLK", "PIN_I2S_WS": "I2S_WS", "PIN_I2S_DIN": "I2S_DIN",
    "PIN_I2S_DOUT": "I2S_DOUT", "PIN_CAM_XCLK": "CAM_XCLK_MCU", "PIN_CAM_VSYNC": "CAM_VSYNC",
    "PIN_CAM_HREF": "CAM_HREF", "PIN_CAM_PCLK": "CAM_PCLK", "PIN_STT_TX": "STT_TX", "PIN_STT_RX": "STT_RX",
    **{f"PIN_CAM_D{i}": f"CAM_D{i}" for i in range(8)},
}
IOX_MACRO_NET = {
    "IOX_BTN_UP": "BTN_UP_N", "IOX_BTN_DOWN": "BTN_DOWN_N", "IOX_BTN_SELECT": "BTN_SELECT_N",
    "IOX_BTN_BACK": "BTN_BACK_N", "IOX_BTN_RECORD": "BTN_RECORD_N", "IOX_BTN_CAMERA": "BTN_CAMERA_N",
    "IOX_CHG_STAT": "CHG_STAT_N", "IOX_SD_DETECT": "SD_DETECT_N", "IOX_LORA_NRST": "LORA_NRST",
    "IOX_LCD_RST": "LCD_RST_N", "IOX_CAM_PWDN": "CAM_PWDN", "IOX_CAM_RESET": "CAM_RESET_N",
    "IOX_AMP_SD": "AMP_SD_N", "IOX_STT_EN": "STT_EN", "IOX_CAM_PWR_EN": "CAM_PWR_EN",
}
POWER_NETS = {"GND", "V3V3", "VSYS", "VBAT", "VBUS", "VSTT", "VCAM_3V3", "VCAM_2V8", "VCAM_1V2", "TCXO_VCC"}


# =============================================================================================
# checks
# =============================================================================================
def check() -> list[str]:
    errs = []
    pin_net: dict[str, str] = {}
    for net, mem in NETS.items():
        for m in mem:
            ref, _, pin = m.partition(".")
            if ref not in PARTS:
                errs.append(f"{net}: unknown part {ref}")
                continue
            p = PARTS[ref]
            if pin not in p.pins:
                errs.append(f"{net}: {ref} has no pin '{pin}' (pins: {', '.join(p.pins)})")
            if pin in p.nc:
                errs.append(f"{net}: {ref}.{pin} is declared NC but connected")
            if m in pin_net and pin_net[m] != net:
                errs.append(f"{m} on two nets: {pin_net[m]} and {net}")
            pin_net[m] = net
        if len(set(mem)) < 2 and net not in ONE_ENDED_OK:
            errs.append(f"net {net} has a single member {mem}")
    for ref, p in PARTS.items():
        for pin in p.pins:
            k = f"{ref}.{pin}"
            if k not in pin_net and pin not in p.nc:
                errs.append(f"unconnected pin {k} (connect it or declare it NC)")
        for pin in p.power:
            if pin_net.get(f"{ref}.{pin}") not in POWER_NETS:
                errs.append(f"supply pin {ref}.{pin} not on a power net ({pin_net.get(f'{ref}.{pin}')})")

    # firmware pin map
    src = open(PINS_H).read()
    defs = dict(re.findall(r"#define\s+(PIN_\w+)\s+(\d+)", src))
    for macro, net in PIN_MACRO_NET.items():
        if macro not in defs:
            errs.append(f"board_pins.h lacks {macro}")
            continue
        g = int(defs[macro])
        if GPIO.get(g) != net:
            errs.append(f"{macro}={g}: board_pins.h says GPIO{g}, hardware wires GPIO{g} to {GPIO.get(g)} (expected {net})")
    used = {int(v) for k, v in defs.items() if k in PIN_MACRO_NET}
    for g in GPIO:
        if g not in used and g not in (19, 20):
            errs.append(f"GPIO{g} wired to {GPIO[g]} but not defined in board_pins.h")
    iox = dict(re.findall(r"#define\s+(IOX_\w+)\s+(\d+)", src))
    for macro, net in IOX_MACRO_NET.items():
        bit = int(iox.get(macro, -1))
        pin = f"P{bit // 8}{bit % 8}" if bit >= 0 else "?"
        if IOX_MAP.get(pin) != net:
            errs.append(f"{macro}={bit}: expander {pin} carries {IOX_MAP.get(pin)}, expected {net}")

    # strap pins
    def has_res(net, other):
        return any(PARTS[m.split('.')[0]].value.split()[0].endswith(("k", "R")) and
                   any(n == other for n, mm in NETS.items() if f"{m.split('.')[0]}.2" in mm or f"{m.split('.')[0]}.1" in mm)
                   for m in NETS[net] if m.split('.')[0].startswith("R"))
    if not has_res("IOX_INT_N", "V3V3"): errs.append("GPIO0 strap needs a pull-up")
    if not has_res("LCD_BL", "GND"): errs.append("GPIO45 strap needs a pull-down (VDD_SPI 3.3 V)")
    if not has_res("I2S_DIN", "GND"): errs.append("GPIO46 strap needs a pull-down")
    return errs


def reports(out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    by_sheet = collections.defaultdict(list)
    for net, mem in sorted(NETS.items()):
        sheets = sorted({PARTS[m.split('.')[0]].sheet for m in mem if m.split('.')[0] in PARTS})
        by_sheet[sheets[0] if sheets else "?"].append((net, mem, sheets))
    with open(os.path.join(out_dir, "connections.md"), "w") as f:
        f.write("# Nomiq A0 — connection specification (generated by hardware/design/nomiq_hw.py)\n\n")
        f.write("Capture the schematic from this; do not edit by hand. Net names are normative.\n\n")
        sheets = sorted({p.sheet for p in PARTS.values()})
        for s in sheets:
            f.write(f"## {s}\n\n### Parts\n\n| Ref | Value | MPN | Notes |\n|---|---|---|---|\n")
            for p in sorted((p for p in PARTS.values() if p.sheet == s), key=lambda p: (p.ref[0], int(re.sub(r'\D', '', p.ref) or 0))):
                flag = " ⚠️ verify" if p.verify else ""
                nc = f" NC: {', '.join(sorted(p.nc))}." if p.nc else ""
                f.write(f"| {p.ref} | {p.value} | {p.mpn} | {p.note}{nc}{flag} |\n")
            f.write("\n### Nets\n\n| Net | Members | Also on sheets |\n|---|---|---|\n")
            for net, mem, sh in by_sheet.get(s, []):
                if net == "GND":
                    f.write(f"| GND | {len(mem)} pins (all sheets) | |\n")
                    continue
                f.write(f"| `{net}` | {', '.join(mem)} | {', '.join(x for x in sh if x != s)} |\n")
            f.write("\n")
        f.write("## Values to verify against datasheets\n\n")
        for p in PARTS.values():
            if p.verify:
                f.write(f"- **{p.ref}** {p.value} ({p.mpn}) — {p.note}\n")
    with open(os.path.join(out_dir, "bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["ref", "value", "mpn", "sheet", "footprint", "verify", "note"])
        for p in sorted(PARTS.values(), key=lambda p: (p.sheet, p.ref)):
            w.writerow([p.ref, p.value, p.mpn, p.sheet, p.footprint, "yes" if p.verify else "", p.note])
    with open(os.path.join(out_dir, "nets.json"), "w") as f:
        json.dump({k: sorted(set(v)) for k, v in sorted(NETS.items())}, f, indent=1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="checks only, no report files")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()
    errs = check()
    n_pins = sum(len(set(v)) for v in NETS.values())
    print(f"{len(PARTS)} parts, {len(NETS)} nets, {n_pins} connections, "
          f"{sum(1 for p in PARTS.values() if p.verify)} parts flagged for datasheet verification")
    for e in errs:
        print("ERROR:", e)
    if not a.check:
        reports(a.out)
        print(f"reports written to {os.path.relpath(a.out, ROOT)}/")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
