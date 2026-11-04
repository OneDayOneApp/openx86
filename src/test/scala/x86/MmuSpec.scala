package x86

import chisel3._
import chiseltest._
import org.scalatest.flatspec.AnyFlatSpec
import x86.core.Mmu

class MmuSpec extends AnyFlatSpec with ChiselScalatestTester {
  private val pml4 = 0x1000L
  private val pdpt = 0x2000L
  private val pd   = 0x3000L
  private val pt   = 0x4000L

  private val tables = Map(
    pml4 + 0L -> 0x2003L,
    pdpt + 0L -> 0x3003L,
    pd + 0L   -> 0x4003L,
    pt + 8L   -> 0x700003L
  )

  private def serve(c: Mmu, mem: Map[Long, Long]): Unit = {
    if (c.io.ptwReq.peek().litToBoolean) {
      val a = c.io.ptwAddr.peek().litValue.toLong
      c.io.ptwData.poke(mem.getOrElse(a, 0L).U)
      c.io.ptwValid.poke(true.B)
    } else {
      c.io.ptwValid.poke(false.B)
    }
  }

  private def translate(c: Mmu, v: Long, write: Boolean, mem: Map[Long, Long],
                        maxCycles: Int = 40): (Long, Boolean, Int) = {
    c.io.reqValid.poke(true.B)
    c.io.vaddr.poke(v.U)
    c.io.isWrite.poke(write.B)
    c.io.isFetch.poke(false.B)
    c.io.pg.poke(true.B)
    c.io.wp.poke(true.B)
    c.io.nxe.poke(true.B)
    c.io.cr3.poke(pml4.U)
    c.clock.step()
    var cycles = 0
    while (!c.io.done.peek().litToBoolean && cycles < maxCycles) {
      if (c.io.ptwReq.peek().litToBoolean) {
        val a = c.io.ptwAddr.peek().litValue.toLong
        println(f"walk addr=0x$a%x data=0x${mem.getOrElse(a, 0L)}%x")
      }
      serve(c, mem)
      c.clock.step()
      cycles += 1
    }
    val paddr = c.io.paddr.peek().litValue.toLong
    val fault = c.io.fault.peek().litToBoolean
    val err = c.io.error.peek().litValue.toInt
    c.io.reqValid.poke(false.B)
    c.io.ptwValid.poke(false.B)
    c.clock.step()
    (paddr, fault, err)
  }

  "mmu" should "translate through four levels" in {
    test(new Mmu) { c =>
      val (p, f, _) = translate(c, 0x001123L, false, tables)
      assert(!f)
      assert(p == 0x700123L, f"paddr=0x$p%x")
    }
  }

  it should "hit the TLB on the second access" in {
    test(new Mmu) { c =>
      val (p1, _, _) = translate(c, 0x001123L, false, tables)
      val (p2, _, _) = translate(c, 0x001456L, false, tables)
      assert(p1 == 0x700123L && p2 == 0x700456L)
    }
  }

  it should "fault on a not-present entry" in {
    test(new Mmu) { c =>
      val (_, f, err) = translate(c, 0x900000L, false, tables)
      assert(f)
      assert((err & 1) == 0, f"err=$err")
    }
  }

  it should "fault on a write to a read-only page" in {
    test(new Mmu) { c =>
      val ro = tables.updated(pt + 8L, 0x700001L) // present, read-only
      val (_, f, err) = translate(c, 0x001000L, true, ro)
      assert(f)
      assert((err & 3) == 3, f"err=$err")
    }
  }
}
