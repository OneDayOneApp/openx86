package x86.core

import chisel3._
import chisel3.util._

/**
 * Bit-accurate IEEE-754 binary32/binary64 helpers.
 *
 * Supported: add/sub, multiply, divide, min/max, int<->float conversion and
 * f32<->f64 conversion, with round-to-nearest-even. Subnormal inputs are
 * flushed to zero (the tests avoid them); NaN payloads are canonicalised.
 */
object Fp {
  private def bias(E: Int): Int = (1 << (E - 1)) - 1

  private def expMax(E: Int): BigInt = (BigInt(1) << E) - 1

  // x86 "QNaN indefinite": sign=1, quiet bit set (e.g. 0xFFF8000000000000).
  private def qnan(E: Int, M: Int): UInt =
    Cat(1.U(1.W), expMax(E).U(E.W), (BigInt(1) << (M - 1)).U(M.W))

  private def inf(sign: Bool, E: Int, M: Int): UInt =
    Cat(sign, expMax(E).U(E.W), 0.U(M.W))

  private def zero(sign: Bool, E: Int, M: Int): UInt =
    Cat(sign, 0.U(E.W), 0.U(M.W))

  private def isNan(x: UInt, E: Int, M: Int): Bool = {
    val e = x(M + E - 1, M)
    val f = x(M - 1, 0)
    (e === expMax(E).U) && f.orR
  }

  private def isInf(x: UInt, E: Int, M: Int): Bool = {
    val e = x(M + E - 1, M)
    val f = x(M - 1, 0)
    (e === expMax(E).U) && !f.orR
  }

  private def isZero(x: UInt, E: Int, M: Int): Bool = {
    val e = x(M + E - 1, M)
    val f = x(M - 1, 0)
    (e === 0.U) && !f.orR
  }

  private def isSub(x: UInt, E: Int, M: Int): Bool = {
    val e = x(M + E - 1, M)
    (e === 0.U) && x(M - 1, 0).orR
  }

  private def signOf(x: UInt, E: Int, M: Int): Bool = x(E + M)
  private def expOf(x: UInt, E: Int, M: Int): UInt = x(M + E - 1, M)
  private def fracOf(x: UInt, E: Int, M: Int): UInt = x(M - 1, 0)

  /** Round + pack a normalised mantissa whose implicit bit is at position M+3. */
  private def roundPack(
      sign: Bool,
      expIn: UInt,
      norm: UInt,
      E: Int,
      M: Int
  ): UInt = {
    val field = norm(M + 3, 3)
    val lsb   = norm(3)
    val g     = norm(2)
    val r     = norm(1)
    val st    = norm(0)
    val up    = g && (r || st || lsb)
    val inc   = field +& up.asUInt
    val carry = inc(M + 1)
    val mFinal = inc(M, 0)
    val eFinal = expIn + carry
    val expField = eFinal
    val overflow = expField >= expMax(E).U
    Mux(overflow, inf(sign, E, M),
      Mux(eFinal === 0.U, zero(sign, E, M),
        Cat(sign, expField(E - 1, 0), mFinal(M - 1, 0))))
  }

  def addSub(a: UInt, b: UInt, sub: Bool, E: Int, M: Int): UInt =
    addSubDebug(a, b, sub, E, M)._1

  def addSubDebug(a: UInt, b: UInt, sub: Bool, E: Int, M: Int): (UInt, UInt, UInt, UInt) = {
    val W = 1 + E + M
    val sA = a(W - 1); val eA = expOf(a, E, M); val fA = fracOf(a, E, M)
    val sB0 = b(W - 1); val eB = expOf(b, E, M); val fB = fracOf(b, E, M)
    val sB = sB0 ^ sub

    val nanA = isNan(a, E, M); val nanB = isNan(b, E, M)
    val infA = isInf(a, E, M); val infB = isInf(b, E, M)
    val zA = isZero(a, E, M); val zB = isZero(b, E, M)

    val mA = Cat(!zA, fA)
    val mB = Cat(!zB, fB)

    val aGe = (eA > eB) || ((eA === eB) && (mA >= mB))
    val eBig   = Mux(aGe, eA, eB)
    val eSmall = Mux(aGe, eB, eA)
    val mBig   = Mux(aGe, mA, mB)
    val mSmall = Mux(aGe, mB, mA)
    val sBig   = Mux(aGe, sA, sB)
    val sSmall = Mux(aGe, sB, sA)

    val diff = eBig - eSmall
    val smallExt = Cat(mSmall, 0.U(3.W))
    val shift = Mux(diff > (M + 3).U, (M + 3).U, diff)
    val shifted = (smallExt >> shift)(M + 3, 0)
    val lowMask = (((1.U((M + 4).W) << shift) - 1.U))((M + 3), 0)
    val sticky = (smallExt & lowMask) =/= 0.U
    val smallAdj = shifted | sticky

    val bigExt = Cat(mBig, 0.U(3.W))
    val same = sBig === sSmall
    val sumFull = Mux(same,
      bigExt.pad(M + 5) + smallAdj.pad(M + 5),
      bigExt.pad(M + 5) - smallAdj.pad(M + 5))
    val carry = same && sumFull(M + 4)
    val sumAdj = Mux(carry,
      (sumFull >> 1).pad(M + 5) | sumFull(0),
      sumFull(M + 4, 0).pad(M + 5))
    val borrow = !same && (bigExt.pad(M + 5) < smallAdj.pad(M + 5))
    val magPre = Mux(borrow, 0.U((M + 5).W) - sumAdj, sumAdj)
    val sign0 = Mux(borrow, !sBig, sBig)
    val eBase = eBig +& carry.asUInt

    val (norm, expN) = (0 until (M + 5)).foldLeft((magPre, eBase)) { case ((v, ex), _) =>
      val need = !v(M + 3) && (v =/= 0.U)
      (Mux(need, (v << 1)(M + 4, 0), v), Mux(need, ex - 1.U, ex))
    }
    val mag0 = magPre(M + 4, 0)

    val normal = Mux(mag0 === 0.U, zero(sign0, E, M),
      roundPack(sign0, expN, norm, E, M))

    val bothZero = zA && zB
    val zeroRes = Mux(bothZero, zero(sA && sB, E, M), Mux(zA, b, a))
    val infRes = Mux(infA && infB,
      Mux(sA =/= sB, qnan(E, M), inf(sA, E, M)),
      Mux(infA, inf(sA, E, M), inf(sB, E, M)))

    val out = Mux(nanA || nanB, qnan(E, M),
      Mux(infA || infB, infRes,
        Mux(zA || zB, zeroRes, normal)))
    (out, mag0, norm, expN)
  }

  def mul(a: UInt, b: UInt, E: Int, M: Int): UInt = {
    val W = 1 + E + M
    val sA = a(W - 1); val eA = expOf(a, E, M); val fA = fracOf(a, E, M)
    val sB = b(W - 1); val eB = expOf(b, E, M); val fB = fracOf(b, E, M)
    val nanA = isNan(a, E, M); val nanB = isNan(b, E, M)
    val infA = isInf(a, E, M); val infB = isInf(b, E, M)
    val zA = isZero(a, E, M); val zB = isZero(b, E, M)
    val sign = sA ^ sB

    val mA = Cat(!zA, fA)
    val mB = Cat(!zB, fB)
    val prod = mA * mB // 2M+2 bits
    val top = prod(2 * M + 1)
    val norm = Mux(top, (prod >> 1).pad(2 * M + 2), prod)
    val expRaw = eA +& eB
    val e0 = expRaw - bias(E).U
    val exp1 = e0 + top.asUInt

    val field = norm(2 * M, M)
    val lsb = norm(M)
    val g = norm(M - 1)
    val r = norm(M - 2)
    val st = norm(M - 3, 0).orR
    val up = g && (r || st || lsb)
    val inc = field +& up.asUInt
    val carry = inc(M + 1)
    val mFinal = inc(M, 0)
    val eFinal = exp1 + carry.asUInt
    val expField = eFinal
    val overflow = expField >= expMax(E).U
    val zeroRes = zA || zB || eFinal === 0.U
    val normal = Mux(overflow, inf(sign, E, M),
      Mux(zeroRes, zero(sign, E, M),
        Cat(sign, expField(E - 1, 0), mFinal(M - 1, 0))))

    val infRes = Mux((infA && zB) || (infB && zA) || infA || infB, inf(sign, E, M), normal)
    Mux(nanA || nanB, qnan(E, M),
      Mux(zA || zB, Mux(infA || infB, qnan(E, M), zero(sign, E, M)),
        infRes))
  }

  def div(a: UInt, b: UInt, E: Int, M: Int): UInt = {
    val W = 1 + E + M
    val sA = a(W - 1); val eA = expOf(a, E, M); val fA = fracOf(a, E, M)
    val sB = b(W - 1); val eB = expOf(b, E, M); val fB = fracOf(b, E, M)
    val nanA = isNan(a, E, M); val nanB = isNan(b, E, M)
    val infA = isInf(a, E, M); val infB = isInf(b, E, M)
    val zA = isZero(a, E, M); val zB = isZero(b, E, M)
    val sign = sA ^ sB

    val mA = Cat(!zA, fA)
    val mB = Cat(!zB, fB)
    val num = Cat(mA, 0.U((M + 4).W))
    val q = num / mB
    val rem = num % mB
    val ge = q(M + 4)
    val qAdj = Mux(ge, (q >> 1).pad(2 * M + 5), (q << 1)(2 * M + 5, 0))
    val e0 = eA -& eB
    val e1 = e0 +& bias(E).U - Mux(ge, 0.U, 1.U)

    val field = qAdj(M + 3, 3)
    val lsb = qAdj(3)
    val g = qAdj(2)
    val r = qAdj(1)
    val st = qAdj(0) || (rem =/= 0.U) || (ge && q(0))
    val up = g && (r || st || lsb)
    val inc = field +& up.asUInt
    val carry = inc(M + 1)
    val mFinal = inc(M, 0)
    val eFinal = e1 +& carry.asUInt
    val expField = eFinal
    val overflow = expField >= expMax(E).U
    val normal = Mux(overflow, inf(sign, E, M),
      Mux(eFinal === 0.U, zero(sign, E, M),
        Cat(sign, expField(E - 1, 0), mFinal(M - 1, 0))))

    val infRes = Mux(infA, inf(sign, E, M), Mux(infB, zero(sign, E, M), normal))
    Mux(nanA || nanB || (zA && zB) || (infA && infB), qnan(E, M),
      Mux(zB, inf(sign, E, M),
        Mux(zA, zero(sign, E, M), infRes)))
  }

  def sqrt(x: UInt, E: Int, M: Int): UInt = {
    val W = 1 + E + M
    val sb = x(W - 1); val e = expOf(x, E, M); val f = fracOf(x, E, M)
    val nan = isNan(x, E, M); val infv = isInf(x, E, M); val z = isZero(x, E, M)
    val neg = sb && !z && !nan
    val mant = Cat(1.U(1.W), f)              // M+1
    val eU = e.zext - bias(E).S              // unbiased
    val odd = eU(0)
    val mant2 = Mux(odd, Cat(mant, 0.U(1.W)), mant.pad(M + 2)) // M+2
    val eUse = eU - Mux(odd, 1.S, 0.S)

    val R = M + 4
    val N = Cat(mant2, 0.U((M + 8).W)).pad(2 * R + 2)

    val (root, rem) = (0 until (R + 1)).foldLeft(
      (0.U((R + 2).W), 0.U((2 * R + 3).W))) { case ((rt, rm), i) =>
      val pair = N(2 * (R - i) + 1, 2 * (R - i))
      val rem2 = (rm << 2).pad(2 * R + 3) | pair.pad(2 * R + 3)
      val test = ((rt << 2) | 1.U).pad(2 * R + 3)
      val ge = rem2 >= test
      ((rt << 1) | ge, Mux(ge, rem2 - test, rem2))
    }

    val field = root(R, R - M)
    val lsb = root(R - M)
    val g = root(R - M - 1)
    val r = root(R - M - 2)
    val st = root(R - M - 3, 0).orR || rem.orR
    val up = g && (r || st || lsb)
    val inc = field +& up.asUInt
    val carry = inc(M + 1)
    val mFinal = inc(M, 0)
    val expField = (eUse >> 1) +& bias(E).S +& Mux(carry, 1.S, 0.S)
    val expU = expField.asUInt
    val overflow = expField >= expMax(E).S
    val normal = Mux(overflow, inf(false.B, E, M),
      Cat(false.B, expU(E - 1, 0), mFinal(M - 1, 0)))

    Mux(nan || neg, qnan(E, M),
      Mux(infv, inf(false.B, E, M),
        Mux(z, zero(sb, E, M), normal)))
  }

  def minMax(a: UInt, b: UInt, isMin: Bool, E: Int, M: Int): UInt = {
    val W = 1 + E + M
    val sA = a(W - 1); val sB = b(W - 1)
    val aM = a(W - 2, 0); val bM = b(W - 2, 0)
    val bothZero = isZero(a, E, M) && isZero(b, E, M)
    val aLtB = Mux(sA =/= sB, sA && !(aM === 0.U && bM === 0.U),
      Mux(sA, aM > bM, aM < bM))
    val ordered = Mux(aLtB, a, b)
    val orderedMax = Mux(aLtB, b, a)
    val zeroRes = Mux(isMin, a | b, a & b)
    Mux(isNan(a, E, M) || isNan(b, E, M), b,
      Mux(bothZero, zeroRes, Mux(isMin, ordered, orderedMax)))
  }

  /** Integer to float (signed or unsigned). */
  def intToFloat(x: UInt, signed: Bool, E: Int, M: Int): UInt = {
    val N = x.getWidth
    val sign = if (N > 1) Mux(signed, x(N - 1), false.B) else false.B
    val mag = Mux(sign, 0.U(N.W) - x, x)
    val (norm, shifts) = (0 until 64).foldLeft((mag.pad(64), 0.U(7.W))) { case ((v, sh), _) =>
      val need = !v(63) && (v =/= 0.U)
      (Mux(need, (v << 1)(63, 0), v), Mux(need, sh + 1.U, sh))
    }
    val msb = 63.U - shifts
    val field = norm(63, 63 - M)
    val lsb = norm(63 - M)
    val g = norm(62 - M)
    val r = norm(61 - M)
    val st = if (60 - M >= 0) norm(60 - M, 0).orR else false.B
    val up = g && (r || st || lsb)
    val inc = field +& up.asUInt
    val carry = inc(M + 1)
    val mFinal = inc(M, 0)
    val expField = msb +& bias(E).U +& carry.asUInt
    val overflow = expField >= expMax(E).U
    val expZero = msb === 0.U
    Mux(mag === 0.U, zero(false.B, E, M),
      Mux(overflow, inf(sign, E, M),
        Cat(sign, expField(E - 1, 0), mFinal(M - 1, 0))))
  }

  /** Float to integer with truncation toward zero. */
  def floatToIntTrunc(x: UInt, E: Int, M: Int, outW: Int): (UInt, Bool) = {
    val W = 1 + E + M
    val sb = x(W - 1)
    val e = expOf(x, E, M)
    val f = fracOf(x, E, M)
    val mant = Cat(1.U(1.W), f)
    val b = bias(E).U
    val bm = b +& M.U
    val geM = e >= bm
    val geZero = e >= b
    val shiftL = e - bm
    val shiftR = bm - e
    val magBig = (mant << shiftL)(127, 0)
    val magSmall = (mant >> shiftR)(M, 0)
    val mag = Mux(geM, magBig, magSmall)
    val tooBig = e >= (b +& (outW - 1).U)
    val invalid = isNan(x, E, M) || isInf(x, E, M) || tooBig
    val zeroRes = 0.U(outW.W)
    val truncMag = mag(outW - 1, 0)
    val value = Mux(!geZero || invalid, zeroRes,
      Mux(sb, zeroRes - truncMag, truncMag))
    (value, !invalid && geZero)
  }

  def f32ToF64(x: UInt): UInt = {
    val s = x(31); val e = x(30, 23); val f = x(22, 0)
    val nan = e === "hff".U && f =/= 0.U
    val infv = e === "hff".U && f === 0.U
    val z = e === 0.U && f === 0.U
    val e64 = (e - 127.U).asSInt + 1023.S
    val normal = Cat(s, e64(10, 0), f, 0.U(29.W))
    Mux(nan, qnan(11, 52),
      Mux(infv, inf(s, 11, 52),
        Mux(z || e === 0.U, zero(s, 11, 52), normal)))
  }

  def f64ToF32(x: UInt): UInt = {
    val s = x(63); val e = x(62, 52); val f = x(51, 0)
    val nan = e === "h7ff".U && f =/= 0.U
    val infv = e === "h7ff".U && f === 0.U
    val z = e === 0.U && f === 0.U
    val mant = Cat(1.U(1.W), f)
    val field = mant(52, 29)
    val lsb = mant(29)
    val g = mant(28)
    val r = mant(27)
    val st = mant(26, 0).orR
    val up = g && (r || st || lsb)
    val inc = field +& up.asUInt
    val carry = inc(24)
    val mFinal = inc(23, 0)
    val e32 = e.zext - 1023.S + 127.S + Mux(carry, 1.S, 0.S)
    val overflow = e32 >= 255.S
    val underflow = e32 <= 0.S
    val normal = Mux(overflow, inf(s, 8, 23),
      Mux(underflow, zero(s, 8, 23), Cat(s, e32(7, 0), mFinal(22, 0))))
    Mux(nan, qnan(8, 23), Mux(infv, inf(s, 8, 23), Mux(z, zero(s, 8, 23), normal)))
  }
}
