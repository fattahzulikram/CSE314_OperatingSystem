#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  struct proc* process = myproc();

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  // While allocating pages to process, we have to check if the number of pages exceeds MAX_TOTAL_PAGES
  // PGROUNDUP rounds page adress up to multiple of PGSIZE
  // Also, ignore init and sh, as they are always in memory, so we'll ignore the limit
  if(PGROUNDUP(newsz) / PGSIZE > MAX_TOTAL_PAGES && process->pid > 2){
    return 0; // Zero on error
  }

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(process->pid <= 2){
      if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
        cprintf("allocuvm out of memory (2)\n");
        deallocuvm(pgdir, newsz, oldsz);
        kfree(mem);
        return 0;
      }
    }else{
      // We need page mappings for the process with pid > 2, mappages do not do this specifically
      char *addr = (char*)PGROUNDDOWN((uint)a);
      pte_t *pte; // Will allocate if necessary
      if( (pte = walkpgdir(pgdir, addr, 1)) == 0 ){
        cprintf("allocuvm out of memory for pid > 2\n");
        deallocuvm(pgdir, newsz, oldsz);
        kfree(mem);
        return 0;
      }
      if(*pte & PTE_P)
        panic("remap");

      int perm = PTE_W | PTE_U;
      int pa = V2P(mem);
      *pte = pa | perm | PTE_P;

      if(process->memoryPageCount >= MAX_PSYC_PAGES){
        SwapPageOut(process);
      }
      #if YUP
        cprintf("Custom area %d\n", process->memoryPageCount);
      #endif
      int emptySlot = GetUnusedMemorySlotFIFO(process); // The new page will be assigned in this slot
      process->allPagesOfProcess[emptySlot].Age = 0; // Start from 0
      process->allPagesOfProcess[emptySlot].pageUsed = 1;  // Slot is in use
      process->allPagesOfProcess[emptySlot].pageExistsInMemory = 1; // Now is in memory
      process->allPagesOfProcess[emptySlot].virtualAddress = (int)addr; // Mapping done
      process->allPagesOfProcess[emptySlot].swapFileLocation = PGSIZE * (process->filePageCount + process->memoryPageCount); // If the page is swapped out, this will be the location where this will be written
      process->memoryPageCount++;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

/********************Necessary Functions********************/
void 
SwapPageOut(struct proc* process)
{
  #if YUP
    cprintf("Swapping page out\n");
  #endif
  int PageToSwapOut;

  #if FIFO
    PageToSwapOut = NextSwappedPageFIFO(process);
  #endif

  #if AGING
    PageToSwapOut = NextSwappedPageAging(process);
  #endif

  pte_t *pte = walkpgdir(process->pgdir, (void*)(process->allPagesOfProcess[PageToSwapOut].virtualAddress), 0);
  if(pte == 0){
    #if YUP
      cprintf("pte is 0 during swap out\n");
    #endif
    return;
  }

  if(!(*pte & PTE_P)){
    #if YUP
      cprintf("P bit is 0 during swap out\n");
    #endif
    return;
  }

  // If the dirty bit is set, we need to write the page to swap file
  if(*pte & PTE_D){
    char *addr = P2V(PTE_ADDR(*pte)); // Get the location where to write the contents
    writeToSwapFile(process, addr, process->allPagesOfProcess[PageToSwapOut].swapFileLocation, PGSIZE);
    kfree(addr);
  }
  // Set the PG bit and reset the P bit
  *pte |= PTE_PG;
  *pte &= ~PTE_P;

  // Update process variables
  process->allPagesOfProcess[PageToSwapOut].Age = 0;
  process->allPagesOfProcess[PageToSwapOut].pageUsed = 1; // Though out of memory, this slot is still in use
  process->allPagesOfProcess[PageToSwapOut].pageExistsInMemory = 0; // Not in ram anymore
  process->memoryPageCount--;
  process->filePageCount++;

  #if YUP
    cprintf("Swapped out page %d of process %s\n", PageToSwapOut, process->name);
  #endif
  return;
}

int 
SwapPageIn(struct proc* process, int virtualAddress)
{
  // Get the pte of the page from the virtual address
  pte_t *pte = walkpgdir(process->pgdir, (void*)virtualAddress, 0);

  if(pte == 0){
    #if YUP
      cprintf("pte is 0 while swapping page in\n");
    #endif
    return -1;
  }
  // pte has to be paged out to be paged in
  if(!(*pte & PTE_PG)){
    #if YUP
      cprintf("pte is not paged out while swapping page in\n");
    #endif
    return -1;
  }
  // Allocate a new memory space
  char *mem = kalloc();
  if(mem == 0){
    #if YUP
      cprintf("Cannot allocate space while swapping in\n");
    #endif
    return -1;
  }
  int pageIndex = -1;
  for(int i=0; i<MAX_TOTAL_PAGES; i++){
    if(process->allPagesOfProcess[i].pageUsed == 1 // Page is used
     && process->allPagesOfProcess[i].pageExistsInMemory == 0 // It is in file
     && process->allPagesOfProcess[i].virtualAddress == virtualAddress // Has the same virtual address as the required page
     ){
       pageIndex = i;
       break;
     }
  }
  if(pageIndex == -1){
    #if YUP
      cprintf("Cannot find the page while swapping in\n");
    #endif
    return -1;
  }

  // Fix the bits again like mappages()
  int perm = PTE_W | PTE_U;
  int pa = V2P(mem);
  *pte = pa | perm | PTE_P;
  // The page won't be paged out anymore
  *pte &= ~PTE_PG;

  // Now, it is possible that the page array in memory is full
  // In that case, we need to swap one out
  int slotToSwapIn = GetUnusedMemorySlotFIFO(process);

  if(slotToSwapIn <= 0){
    SwapPageOut(process);
  }
  // Now get the position again
  slotToSwapIn = GetUnusedMemorySlotFIFO(process);
  // This should give us a valid slot
  // Now, we update process structure
  process->allPagesOfProcess[pageIndex].pageUsed = 0; // It is now an empty slot
  process->allPagesOfProcess[pageIndex].pageExistsInMemory = 0;

  process->allPagesOfProcess[slotToSwapIn].Age = 0; // Start from 0
  process->allPagesOfProcess[slotToSwapIn].pageExistsInMemory = 1; // It's back on memory
  process->allPagesOfProcess[slotToSwapIn].pageUsed = 1; // Slot is in use
  process->allPagesOfProcess[slotToSwapIn].swapFileLocation = process->allPagesOfProcess[pageIndex].swapFileLocation; // Retain the spot in swap file
  process->allPagesOfProcess[slotToSwapIn].virtualAddress = process->allPagesOfProcess[pageIndex].virtualAddress; // Retain the virtual address
  process->filePageCount--;
  process->memoryPageCount++;
  return 0; // Success
}

/********************FIFO********************/

// Returns the next free slot in the array
int 
GetUnusedMemorySlotFIFO(struct proc* process)
{
  if(process == 0){
    panic("Panic: No process found GetUnusedMemorySlotFIFO\n");
  }
  // If there is no space available, return error
  if(process->memoryPageCount >= MAX_PSYC_PAGES){
    #if YUP
      cprintf("All slots full\n");
    #endif
    return -1;
  }
  // If code comes here, it means that at least one space is available
  // Start from the index before the previous start index and traverse the array backwards
  // When an entry is occupied, it is the newest one. So, we return the entry next to it
  int Start;
  if(process->startIndexFIFO == 0){
    Start = MAX_PSYC_PAGES - 1;
  }else{
    Start = process->startIndexFIFO - 1;
  }
  // This loop returns the indext after the newest inserted index
  for(int i=Start; i != process->startIndexFIFO; i--){
    // Select the farthest entry from the start index by going backwards
    if(process->allPagesOfProcess[i].pageUsed == 1){
      return (i+1)%MAX_PSYC_PAGES;
    }
    if(i==0){
        i = MAX_PSYC_PAGES;
    }
  }
  // The previous loop does not check the start index. If it comes here, it means apart from the start index, the array is empty
  // So return the index after start index
  if(process->allPagesOfProcess[process->startIndexFIFO].pageUsed == 1){
    return (process->startIndexFIFO + 1) % MAX_PSYC_PAGES;
  }
  // If at least one slot was occupied, the code wont't come here. If code comes here, it means that all slots are empty. So return first index
  return 0;
}

// Checks if update in start index is necessary
void
UpdateRemovedStartIndex(struct proc* process)
{
  if(process == 0){
    return;
  }
  // FIFO only works on pages that belong in the memory. If it does not belong in the memory (Maybe on file or deleted), we need to update the start index
  if(process->allPagesOfProcess[process->startIndexFIFO].pageExistsInMemory == 0){
    UpdateStartIndexFIFO(process);
  }
}

// Updates the start index
void 
UpdateStartIndexFIFO(struct proc* process)
{
  if(process == 0){
    return;
  }
  int i;
  // If no page is in memory, we will use the 0th index as start index
  // Otherwise
  if(process->memoryPageCount != 0){
      i = (process->startIndexFIFO+1)%MAX_PSYC_PAGES;
      // If we find a page that exists in memory after the last start index, that will be the new start index
      while(process->allPagesOfProcess[i].pageExistsInMemory != 1){
          i = (i + 1) % MAX_PSYC_PAGES;
      }
  }else{
      // When there is no page in memory
      i = 0;
  }
  process->startIndexFIFO = i;
}

// Returns the start index of the fifo, which implies the oldest page in memory
int
NextSwappedPageFIFO(struct proc* process)
{
  // FIFO - so return the oldest (Starting) entry
  return process->startIndexFIFO;
}

/********************Aging********************/
// Will be called during timer interruption. We will update the aging info here
// We will assume 32 bit age variable
void 
UpdateAgingState()
{
  #if YUP
    cprintf("Updating ages\n");
  #endif
  struct proc *p;
  acquire(&ptable.lock);
  // Iterate through all the processes
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    // Only check the relevent process types
    if(p->pid > 2 && (p->state == SLEEPING || p->state == RUNNABLE || p->state == RUNNING)){
      for(int i=0; i<MAX_TOTAL_PAGES; i++){
        // Only eligible for pages in memory
        if(p->allPagesOfProcess[i].pageExistsInMemory == 1 && p->allPagesOfProcess[i].pageUsed == 1){
          // Get the pte entry for checking the flags
          int pageDirAddr = PDX(p->allPagesOfProcess[i].virtualAddress); // Get 10 MSB, which accesses the page directory
          int pageTableAddr = PTX(p->allPagesOfProcess[i].virtualAddress); // Get the next 10 bits to access the page table
          int pteFlags = PTE_FLAGS(((pte_t*)P2V(PTE_ADDR(p->pgdir[pageDirAddr])))[pageTableAddr]); // Get the flags

          //pte_t* pte = walkpgdir(p->allPagesOfProcess[i].pgdir, (char*)p->pageInMemory[i].virtualAddress, 0);
          // Check reference bit
          if(pteFlags & PTE_A){
            // If page was referenced, divide age by 2 and set the msb of counter
            p->allPagesOfProcess[i].Age = p->allPagesOfProcess[i].Age >> 1;
            p->allPagesOfProcess[i].Age = p->allPagesOfProcess[i].Age | 1 << 31;
          }else{
            // If was not referenced, only divide age by 2
            p->allPagesOfProcess[i].Age = p->allPagesOfProcess[i].Age >> 1;
          }
          // Clear the reference bit
          ((pte_t*)P2V(PTE_ADDR(p->pgdir[pageDirAddr])))[pageTableAddr] &= ~PTE_A;
        }
      }
    }
  }
  release(&ptable.lock);
}

// From all the pages, the page with the lowest age value will be selected for swapping
int 
NextSwappedPageAging(struct proc* process)
{
  int MinimumPageIndex = -1;
  uint MinimumAge = 0xffffffff;
  for(int i=0; i<MAX_TOTAL_PAGES; i++){
    // Only for pages inside memory and which is valid
    if(process->allPagesOfProcess[i].pageExistsInMemory == 1 && process->allPagesOfProcess[i].pageUsed == 1){
      #if YUP
        cprintf("Process: %d, Page: %d, Age: %d\n", process->pid, i, process->allPagesOfProcess[i].Age);
      #endif
      if(process->allPagesOfProcess[i].Age < MinimumAge){
        MinimumAge = process->allPagesOfProcess[i].Age;
        MinimumPageIndex = i;
      }
    }
  }
  #if YUP
    cprintf("Swapping page %d out of process id %d, Age: %d\n", MinimumPageIndex, process->pid, MinimumAge);
  #endif
  return MinimumPageIndex;
}