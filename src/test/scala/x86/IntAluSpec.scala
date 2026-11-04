package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.IntAlu
import x86.isa.{FlagBit, Ops}

class IntAluSpec extends AnyFlatSpec with ChiselScalatestTester {
  private def op(c: IntAlu, o: Int, a: BigInt, b: BigInt, sz: Int, flags: BigInt = 0x202): Unit = {
    c.io.op.poke(o.U)
    c.io.a.poke(a.U)
    c.io.b.poke(b.U)
    c.io.opsz.poke(sz.U)
    c.io.flagsIn.poke(flags.U)
    c.io.cond.poke(0.U)
    c.io.imm.poke(0.U)
    c.io.addrIn.poke(0.U)
    c.io.hiIn.poke(0.U)
    c.io.oneOp.poke(false.B)
  }

  "int alu" should "set AF and CF on NEG with nonzero low nibble" in {
    test(new IntAlu) { c =>
      op(c, Ops.NEG, 3, 0, 8)
      c.io.result.expect("hfffffffffffffffd".U)
      c.io.flags.expect((0x202 | 0x1 | 0x10 | 0x80).U) // CF, AF, SF
    }
  }

  it should "not set AF on NEG of a multiple of 16" in {
    test(new IntAlu) { c =>
      op(c, Ops.NEG, 0x10, 0, 8)
      c.io.result.expect("hfffffffffffffff0".U)
      val f = c.io.flags.peek().litValue
      assert((f & (BigInt(1) << FlagBit.AF)) == 0, f"flags=0x$f%x")
      assert((f & (BigInt(1) << FlagBit.CF)) != 0)
    }
  }

  it should "keep DF when CLD/STD are not involved" in {
    test(new IntAlu) { c =>
      op(c, Ops.ADD, 1, 2, 4)
      c.io.result.expect(3.U)
      val f = c.io.flags.peek().litValue
      assert((f & (BigInt(1) << FlagBit.DF)) == 0)
    }
  }
}
