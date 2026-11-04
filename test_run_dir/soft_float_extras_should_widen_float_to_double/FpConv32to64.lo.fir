FIRRTL version 1.1.0
circuit FpConv32to64 :
  module FpConv32to64 :
    input clock : Clock
    input reset : UInt<1>
    input io_x : UInt<32>
    output io_out : UInt<64>

    node io_out_s = bits(io_x, 31, 31) @[FpUnit.scala 339:14]
    node io_out_e = bits(io_x, 30, 23) @[FpUnit.scala 339:29]
    node io_out_f = bits(io_x, 22, 0) @[FpUnit.scala 339:48]
    node _io_out_nan_T = eq(io_out_e, UInt<8>("hff")) @[FpUnit.scala 340:17]
    node _io_out_nan_T_1 = neq(io_out_f, UInt<1>("h0")) @[FpUnit.scala 340:34]
    node io_out_nan = and(_io_out_nan_T, _io_out_nan_T_1) @[FpUnit.scala 340:29]
    node _io_out_infv_T = eq(io_out_e, UInt<8>("hff")) @[FpUnit.scala 341:18]
    node _io_out_infv_T_1 = eq(io_out_f, UInt<1>("h0")) @[FpUnit.scala 341:35]
    node io_out_infv = and(_io_out_infv_T, _io_out_infv_T_1) @[FpUnit.scala 341:30]
    node _io_out_z_T = eq(io_out_e, UInt<1>("h0")) @[FpUnit.scala 342:15]
    node _io_out_z_T_1 = eq(io_out_f, UInt<1>("h0")) @[FpUnit.scala 342:28]
    node io_out_z = and(_io_out_z_T, _io_out_z_T_1) @[FpUnit.scala 342:23]
    node _io_out_e64_T = sub(io_out_e, UInt<7>("h7f")) @[FpUnit.scala 343:18]
    node _io_out_e64_T_1 = tail(_io_out_e64_T, 1) @[FpUnit.scala 343:18]
    node _io_out_e64_T_2 = asSInt(_io_out_e64_T_1) @[FpUnit.scala 343:27]
    node _io_out_e64_T_3 = add(_io_out_e64_T_2, asSInt(UInt<11>("h3ff"))) @[FpUnit.scala 343:34]
    node _io_out_e64_T_4 = tail(_io_out_e64_T_3, 1) @[FpUnit.scala 343:34]
    node io_out_e64 = asSInt(_io_out_e64_T_4) @[FpUnit.scala 343:34]
    node _io_out_normal_T = bits(io_out_e64, 10, 0) @[FpUnit.scala 344:28]
    node io_out_normal_lo = cat(io_out_f, UInt<29>("h0")) @[Cat.scala 33:92]
    node io_out_normal_hi = cat(io_out_s, _io_out_normal_T) @[Cat.scala 33:92]
    node io_out_normal = cat(io_out_normal_hi, io_out_normal_lo) @[Cat.scala 33:92]
    node io_out_hi = cat(UInt<1>("h1"), UInt<11>("h7ff")) @[Cat.scala 33:92]
    node _io_out_T = cat(io_out_hi, UInt<52>("h8000000000000")) @[Cat.scala 33:92]
    node io_out_hi_1 = cat(io_out_s, UInt<11>("h7ff")) @[Cat.scala 33:92]
    node _io_out_T_1 = cat(io_out_hi_1, UInt<52>("h0")) @[Cat.scala 33:92]
    node _io_out_T_2 = eq(io_out_e, UInt<1>("h0")) @[FpUnit.scala 347:20]
    node _io_out_T_3 = or(io_out_z, _io_out_T_2) @[FpUnit.scala 347:15]
    node io_out_hi_2 = cat(io_out_s, UInt<11>("h0")) @[Cat.scala 33:92]
    node _io_out_T_4 = cat(io_out_hi_2, UInt<52>("h0")) @[Cat.scala 33:92]
    node _io_out_T_5 = mux(_io_out_T_3, _io_out_T_4, io_out_normal) @[FpUnit.scala 347:12]
    node _io_out_T_6 = mux(io_out_infv, _io_out_T_1, _io_out_T_5) @[FpUnit.scala 346:10]
    node _io_out_T_7 = mux(io_out_nan, _io_out_T, _io_out_T_6) @[FpUnit.scala 345:8]
    io_out <= _io_out_T_7 @[FpSpec2.scala 40:10]
