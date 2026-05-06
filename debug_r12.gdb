set width 0
set height 0
handle SIGSEGV nostop
handle SIGTRAP stop

# Host breakpoint at LoadLibraryA entry
break *LoadLibraryA
commands
  silent
  printf "=== LoadLibraryA entry: RAX=%lx R12=%lx\n", $rax, $r12
  continue
end

# Host breakpoint at LoadLibraryA return (address 0xa2ad = a1d0 + fd - 1 = ret at a2ad)
break *0xa2ad
commands
  silent
  printf "=== LoadLibraryA ret (a2ad): RAX=%lx R12=%lx\n", $rax, $r12
  continue
end

# Guest breakpoints - use pending
set breakpoint pending on

break *0x1400029df
commands
  silent
  printf "=== Guest 0x1400029df (after LoadLibraryA, mov rax,r12): RAX=%lx R12=%lx\n", $rax, $r12
  continue
end

break *0x140002a01
commands
  silent
  printf "=== Guest 0x140002a01 (before lstrcpyA): R12=%lx\n", $r12
  continue
end

break *0x140002a03
commands
  silent
  printf "=== Guest 0x140002a03 (after lstrcpyA): R12=%lx\n", $r12
  continue
end

break *0x140002a06
commands
  silent
  printf "=== Guest 0x140002a06 (before out): R12=%lx\n", $r12
  continue
end

break *0x140002a0b
commands
  silent
  printf "=== Guest 0x140002a0b (after out): R12=%lx\n", $r12
  continue
end

break *0x140002a0e
commands
  silent
  printf "=== Guest 0x140002a0e (before out_ptr): R12=%lx\n", $r12
  continue
end

run samples/dll_loader/dll_loader.exe
