// exception.cc
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "console.h"
#include "synch.h"

// THIS IS ONLY FOR TEST PURPOSE REMOVE THIS WHILE SUBMITTING CODE
#include <typeinfo>

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions
//	are in machine.h.
//----------------------------------------------------------------------
static Semaphore *readAvail;
static Semaphore *writeDone;
static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

static void ConvertIntToHex (unsigned v, Console *console)
{
   unsigned x;
   if (v == 0) return;
   ConvertIntToHex (v/16, console);
   x = v % 16;
   if (x < 10) {
      writeDone->P() ;
      console->PutChar('0'+x);
   }
   else {
      writeDone->P() ;
      console->PutChar('a'+x-10);
   }
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);
    int memval, vaddr, printval, tempval, exp;
    unsigned printvalus;        // Used for printing in hex
    if (!initializedConsoleSemaphores) {
       readAvail = new Semaphore("read avail", 0);
       writeDone = new Semaphore("write done", 1);
       initializedConsoleSemaphores = true;
    }
    Console *console = new Console(NULL, NULL, ReadAvail, WriteDone, 0);;

    if ((which == SyscallException) && (type == SysCall_Halt)) {
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
    }
    else if ((which == SyscallException) && (type == SysCall_PrintInt)) {
       printval = machine->ReadRegister(4);
       if (printval == 0) {
	  writeDone->P() ;
          console->PutChar('0');
       }
       else {
          if (printval < 0) {
	     writeDone->P() ;
             console->PutChar('-');
             printval = -printval;
          }
          tempval = printval;
          exp=1;
          while (tempval != 0) {
             tempval = tempval/10;
             exp = exp*10;
          }
          exp = exp/10;
          while (exp > 0) {
	     writeDone->P() ;
             console->PutChar('0'+(printval/exp));
             printval = printval % exp;
             exp = exp/10;
          }
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintChar)) {
	writeDone->P() ;
        console->PutChar(machine->ReadRegister(4));   // echo it!
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintString)) {
       vaddr = machine->ReadRegister(4);
       machine->ReadMem(vaddr, 1, &memval);
       while ((*(char*)&memval) != '\0') {
	  writeDone->P() ;
          console->PutChar(*(char*)&memval);
          vaddr++;
          machine->ReadMem(vaddr, 1, &memval);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintIntHex)) {
       printvalus = (unsigned)machine->ReadRegister(4);
       writeDone->P() ;
       console->PutChar('0');
       writeDone->P() ;
       console->PutChar('x');
       if (printvalus == 0) {
          writeDone->P() ;
          console->PutChar('0');
       }
       else {
          ConvertIntToHex (printvalus, console);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
	else if ((which == SyscallException) && (type == SysCall_GetReg)){
       machine->WriteRegister(2,(unsigned)machine->ReadRegister(machine->ReadRegister(4)));
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
}
	else if ((which == SyscallException) && (type== SysCall_GetPA)){

		    int i;
		    unsigned int vpn, offset;
		    TranslationEntry *entry;
		    unsigned int pageFrame;
			  bool isError=false;

			  int virtAddr=(unsigned)machine->ReadRegister(4);

		    // we must have either a TLB or a page table, but not both!
		    ASSERT(machine->tlb == NULL || machine->KernelPageTable == NULL);
		    ASSERT(machine->tlb != NULL || (machine->KernelPageTable) != NULL);

		// calculate the virtual page number, and offset within the page,
		// from the virtual address
		    vpn = (unsigned) virtAddr / PageSize;
		    offset = (unsigned) virtAddr % PageSize;

		    if ((machine->tlb) == NULL) {		// => page table => vpn is index into table
                if (vpn >= machine->pageTableSize) {
                    DEBUG('a', "virtual page # %d too large for page table size %d!\n",
                        virtAddr, machine->pageTableSize);
                    isError=true;
                } else if (!(machine->KernelPageTable)[vpn].valid) {
                    DEBUG('a', "virtual page # %d too large for page table size %d!\n",
                        virtAddr, machine->pageTableSize);
                    isError=true;
                }
                entry = &(machine->KernelPageTable)[vpn];
		    } else {
                for (entry = NULL, i = 0; i < TLBSize; i++)
                    if ((machine->tlb)[i].valid && ((machine->tlb)[i].virtualPage == (unsigned)vpn)) {
                    entry = &(machine->tlb)[i];			// FOUND!
                    break;
                    }
                if (entry == NULL) {				// not found
                    DEBUG('a', "*** no valid TLB entry found for this virtual page!\n");
                    isError=true;
                }
		    }
            if(!isError){
                pageFrame = entry->physicalPage;

                // if the pageFrame is too big, there is something really wrong!
                // An invalid translation was loaded into the page table or TLB.
                if (pageFrame >= NumPhysPages) {
                    DEBUG('a', "*** frame %d > %d!\n", pageFrame, NumPhysPages);
                    isError=true;
                }
                else{
                    entry->use = TRUE;		// set the use, dirty bits
                    machine->WriteRegister(2, pageFrame * PageSize + offset);
                }
            }
            if(isError){
                machine->WriteRegister(2,-1);
            }
           machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
           machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
           machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

        }
        else if((which == SyscallException) && (type==SysCall_GetPID)){
           machine->WriteRegister(2,currentThread->getPID());
           machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
           machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
           machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
        }
        else if((which == SyscallException) && (type==SysCall_GetPPID)){
           machine->WriteRegister(2,currentThread->getPPID());
           machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
           machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
           machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
        }
	else if ((type== SysCall_Time)){

		machine->WriteRegister(2,stats->totalTicks);
     machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
     machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
     machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
	}
	else if((type==SysCall_Yield)){
 machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
 machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
 machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
		currentThread->YieldCPU();
	}
	else if ((type==SysCall_Sleep)){
		int sticks=machine->ReadRegister(4);
		ASSERT(sticks>=0);
		 machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
     machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
     machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
		if(sticks==0){
			currentThread->YieldCPU();
		}else{
			// CHECK WHETHER THEV OVERALL PAHTWAY ALSO INCREMEMENTS THE PC OR NOT?
			currentThread->addToThreadSleepIntList(currentThread,sticks);
			currentThread->PutThreadToSleep();
		}
	}
  else if((which == SyscallException) && (type==SysCall_NumInstr)){
    machine->WriteRegister(2, currentThread->instrCount);
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
  }
	else{
	printf("Unexpecte user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
	//printf("Total tics =%d",stats->totalTicks);



	//m	 DEBUG('a', "current thread %d\n",currentThread->pid);

}
