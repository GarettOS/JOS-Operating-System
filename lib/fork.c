// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;


	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    if (!(err & FEC_WR) && (uvpd[PDX(addr)] & (PTE_P & PTE_COW))) {
        panic("write fault on a read-only page");

    }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    if ((r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_W | PTE_U)) < 0) {
        panic("could not alloc page in pgfault");
    }
    memcpy(PFTEMP, (void *)PTE_ADDR(addr), PGSIZE);
    if ((r = sys_page_map(0, PFTEMP, 0, (void *)PTE_ADDR(addr), PTE_U | PTE_P | PTE_W)) < 0) {
        panic("could not copy faulted page in pgfault");
    }
    if (sys_page_unmap(0, PFTEMP) < 0) {
        panic("unmap panic in pgfault");
    }
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
    // our virtual table pages address
    void* vpa = (void*)(pn*PGSIZE);
    pte_t pte = uvpt[pn];
    // if the page is writable or copy on write
    if ((pte & PTE_COW) || (pte & PTE_W)) {
        if ((r = sys_page_map(0, vpa, envid, vpa, PTE_P | PTE_U | PTE_COW))< 0) {
            panic("child address failed to map in duppage");
        }
        if ((r = sys_page_map(0, vpa, 0, vpa, PTE_P | PTE_U | PTE_COW)) < 0) {
            panic("parent address failed to map in duppage");
        }
    }
    else {
        sys_page_map(0, vpa, envid, vpa, PTE_P | PTE_U);
    }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
    set_pgfault_handler(pgfault);
	// LAB 4: Your code here.
    envid_t envid;
    uint32_t addr;
    int r;

    // allocate the child environment
    envid = sys_exofork();
    if (envid < 0)
        panic ("sys_exofork: %e", envid);
    if (envid == 0) {
        // we are child
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }
    // copy just the mappings
    for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
        int i = PGNUM(addr);
        if (uvpd[PDX(addr)] & PTE_P) {
            if (uvpt[i] & PTE_P) {
                if (uvpt[i] & PTE_U) {
                    duppage(envid, i);
                }
            }
        }
    }

    // allocate a new page at uxstacktop-pgsize for separate exception stack
    if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_P | PTE_U | PTE_W)) < 0) {
        panic("sys_page_alloc panic in fork");
    }

    // set child page fault handler
    extern void _pgfault_upcall();
    if (sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0) {
        panic("sys_env_set_pgfault_upcall panic in fork");
    }
    // make child runnablle after finishing the COW
    if (sys_env_set_status(envid, ENV_RUNNABLE) < 0) {
        panic("env_set_status panic in fork");
    }

    return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
