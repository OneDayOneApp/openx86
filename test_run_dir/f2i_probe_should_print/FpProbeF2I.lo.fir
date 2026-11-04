FIRRTL version 1.1.0
circuit FpProbeF2I :
  module FpProbeF2I :
    input clock : Clock
    input reset : UInt<1>
    input io_x : UInt<64>
    output io_out : UInt<64>
    output io_ok : UInt<1>
    output io_e : UInt<11>
    output io_geZero : UInt<1>
    output io_tooBig : UInt<1>
    output io_nan : UInt<1>
    output io_inf : UInt<1>

    node e = bits(io_x, 62, 52) @[FpProbe2.scala 19:15]
    node f = bits(io_x, 51, 0) @[FpProbe2.scala 20:15]
    node _io_nan_T = eq(e, UInt<11>("h7ff")) @[FpProbe2.scala 22:16]
    node _io_nan_T_1 = neq(f, UInt<1>("h0")) @[FpProbe2.scala 22:34]
    node _io_nan_T_2 = and(_io_nan_T, _io_nan_T_1) @[FpProbe2.scala 22:28]
    node _io_inf_T = eq(e, UInt<11>("h7ff")) @[FpProbe2.scala 23:16]
    node _io_inf_T_1 = eq(f, UInt<1>("h0")) @[FpProbe2.scala 23:34]
    node _io_inf_T_2 = and(_io_inf_T, _io_inf_T_1) @[FpProbe2.scala 23:28]
    node _io_geZero_T = geq(e, UInt<10>("h3ff")) @[FpProbe2.scala 24:18]
    node _io_tooBig_T = geq(e, UInt<11>("h43e")) @[FpProbe2.scala 25:18]
    node sb = bits(io_x, 63, 63) @[FpUnit.scala 316:15]
    node e_1 = bits(io_x, 62, 52) @[FpUnit.scala 52:55]
    node f_1 = bits(io_x, 51, 0) @[FpUnit.scala 53:56]
    node mant = cat(UInt<1>("h1"), f_1) @[Cat.scala 33:92]
    node bm = add(UInt<10>("h3ff"), UInt<6>("h34")) @[FpUnit.scala 321:16]
    node geM = geq(e_1, bm) @[FpUnit.scala 322:17]
    node geZero = geq(e_1, UInt<10>("h3ff")) @[FpUnit.scala 323:20]
    node _shiftL_T = sub(e_1, bm) @[FpUnit.scala 324:20]
    node shiftL = tail(_shiftL_T, 1) @[FpUnit.scala 324:20]
    node _shiftR_T = sub(bm, e_1) @[FpUnit.scala 325:21]
    node shiftR = tail(_shiftR_T, 1) @[FpUnit.scala 325:21]
    node _magBig_T = dshl(mant, shiftL) @[FpUnit.scala 326:24]
    node magBig = bits(_magBig_T, 127, 0) @[FpUnit.scala 326:34]
    node _magSmall_T = dshr(mant, shiftR) @[FpUnit.scala 327:26]
    node magSmall = bits(_magSmall_T, 52, 0) @[FpUnit.scala 327:36]
    node mag = mux(geM, magBig, magSmall) @[FpUnit.scala 328:18]
    node _tooBig_T = add(UInt<10>("h3ff"), UInt<6>("h3f")) @[FpUnit.scala 329:26]
    node tooBig = geq(e_1, _tooBig_T) @[FpUnit.scala 329:20]
    node invalid_e = bits(io_x, 62, 52) @[FpUnit.scala 29:14]
    node invalid_f = bits(io_x, 51, 0) @[FpUnit.scala 30:14]
    node _invalid_T = eq(invalid_e, UInt<11>("h7ff")) @[FpUnit.scala 31:8]
    node _invalid_T_1 = orr(invalid_f) @[FpUnit.scala 31:30]
    node _invalid_T_2 = and(_invalid_T, _invalid_T_1) @[FpUnit.scala 31:25]
    node invalid_e_1 = bits(io_x, 62, 52) @[FpUnit.scala 35:14]
    node invalid_f_1 = bits(io_x, 51, 0) @[FpUnit.scala 36:14]
    node _invalid_T_3 = eq(invalid_e_1, UInt<11>("h7ff")) @[FpUnit.scala 37:8]
    node _invalid_T_4 = orr(invalid_f_1) @[FpUnit.scala 37:31]
    node _invalid_T_5 = eq(_invalid_T_4, UInt<1>("h0")) @[FpUnit.scala 37:28]
    node _invalid_T_6 = and(_invalid_T_3, _invalid_T_5) @[FpUnit.scala 37:25]
    node _invalid_T_7 = or(_invalid_T_2, _invalid_T_6) @[FpUnit.scala 330:34]
    node invalid = or(_invalid_T_7, tooBig) @[FpUnit.scala 330:52]
    node truncMag = bits(mag, 63, 0) @[FpUnit.scala 332:23]
    node _value_T = eq(geZero, UInt<1>("h0")) @[FpUnit.scala 333:21]
    node _value_T_1 = or(_value_T, invalid) @[FpUnit.scala 333:29]
    node _value_T_2 = sub(UInt<64>("h0"), truncMag) @[FpUnit.scala 334:23]
    node _value_T_3 = tail(_value_T_2, 1) @[FpUnit.scala 334:23]
    node _value_T_4 = mux(sb, _value_T_3, truncMag) @[FpUnit.scala 334:10]
    node v = mux(_value_T_1, UInt<64>("h0"), _value_T_4) @[FpUnit.scala 333:20]
    node _T = eq(invalid, UInt<1>("h0")) @[FpUnit.scala 335:13]
    node ok = and(_T, geZero) @[FpUnit.scala 335:22]
    io_out <= v @[FpProbe2.scala 27:10]
    io_ok <= ok @[FpProbe2.scala 27:22]
    io_e <= e @[FpProbe2.scala 21:8]
    io_geZero <= _io_geZero_T @[FpProbe2.scala 24:13]
    io_tooBig <= _io_tooBig_T @[FpProbe2.scala 25:13]
    io_nan <= _io_nan_T_2 @[FpProbe2.scala 22:10]
    io_inf <= _io_inf_T_2 @[FpProbe2.scala 23:10]
