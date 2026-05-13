/* Function-level tests for the VM public API. */

#include "tests/threads/tests.h"
#include <debug.h>
#include <stdbool.h>
#include <stdint.h>
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

static void init_current_spt (void);
static void init_spt (struct supplemental_page_table *);
static void ensure_current_pml4 (void);
static void *unit_addr (size_t page_no);
static void *unit_addr_ofs (size_t page_no, size_t ofs);
static struct page *new_uninit_page (void *, enum vm_type, bool);
static bool unit_initializer (struct page *, void *aux);

static void
init_current_spt (void)
{
  init_spt (&thread_current ()->spt);
}

static void
init_spt (struct supplemental_page_table *spt)
{
  if (!supplemental_page_table_init (spt))
    fail ("supplemental_page_table_init returned false");
}

static void
ensure_current_pml4 (void)
{
  struct thread *cur = thread_current ();

  if (cur->pml4 == NULL)
    {
      cur->pml4 = pml4_create ();
      if (cur->pml4 == NULL)
        fail ("pml4_create returned NULL");
      pml4_activate (cur->pml4);
    }
}

static void *
unit_addr (size_t page_no)
{
  return (void *) (0x8048000 + page_no * PGSIZE);
}

static void *
unit_addr_ofs (size_t page_no, size_t ofs)
{
  return (void *) ((uintptr_t) unit_addr (page_no) + ofs);
}

static struct page *
new_uninit_page (void *va, enum vm_type type, bool writable)
{
  bool (*initializer) (struct page *, enum vm_type, void *);
  struct page *page;

  switch (VM_TYPE (type))
    {
    case VM_ANON:
      initializer = anon_initializer;
      break;
    case VM_FILE:
      initializer = file_backed_initializer;
      break;
    default:
      fail ("unsupported test page type %d", VM_TYPE (type));
    }

  page = malloc (sizeof *page);
  if (page == NULL)
    fail ("malloc returned NULL");

  uninit_new (page, va, NULL, type, NULL, initializer);
  page->writable = writable;
  return page;
}

static bool
unit_initializer (struct page *page UNUSED, void *aux UNUSED)
{
  return true;
}

void
test_vm_page_get_type (void)
{
  struct page *anon;
  struct page *file;

  init_current_spt ();
  anon = new_uninit_page (unit_addr (0), VM_ANON | VM_MARKER_0, true);
  file = new_uninit_page (unit_addr (1), VM_FILE | VM_MARKER_1, false);

  if (page_get_type (anon) != VM_ANON || page_get_type (file) != VM_FILE)
    fail ("page_get_type did not resolve uninit page type");
  msg ("uninit types resolve");

  anon_initializer (anon, VM_ANON, NULL);
  file_backed_initializer (file, VM_FILE, NULL);
  if (page_get_type (anon) != VM_ANON || page_get_type (file) != VM_FILE)
    fail ("page_get_type did not resolve initialized page type");
  msg ("initialized types resolve");

  vm_dealloc_page (anon);
  vm_dealloc_page (file);
  pass ();
}

void
test_vm_anon_initializer (void)
{
  struct page *page;

  init_current_spt ();
  page = new_uninit_page (unit_addr (2), VM_ANON, true);
  if (!anon_initializer (page, VM_ANON, NULL))
    fail ("anon_initializer returned false");
  if (page_get_type (page) != VM_ANON)
    fail ("anon_initializer did not install anon operations");

  vm_dealloc_page (page);
  msg ("anon page operations installed");
  pass ();
}

void
test_vm_file_initializer (void)
{
  struct page *page;

  init_current_spt ();
  page = new_uninit_page (unit_addr (3), VM_FILE, false);
  if (!file_backed_initializer (page, VM_FILE, NULL))
    fail ("file_backed_initializer returned false");
  if (page_get_type (page) != VM_FILE)
    fail ("file_backed_initializer did not install file operations");

  vm_dealloc_page (page);
  msg ("file page operations installed");
  pass ();
}

void
test_vm_spt_init (void)
{
  struct supplemental_page_table spt;

  init_current_spt ();
  init_spt (&spt);
  msg ("init returned true");

  if (spt_find_page (&spt, unit_addr (4)) != NULL)
    fail ("empty SPT lookup returned a page");
  msg ("missing lookup null");

  supplemental_page_table_kill (&spt);
  pass ();
}

void
test_vm_spt_insert (void)
{
  struct supplemental_page_table spt;
  struct page *page;
  struct page *dup;
  struct page *misaligned;
  struct page *kernel;
  struct page *low;

  init_current_spt ();
  init_spt (&spt);

  page = new_uninit_page (unit_addr (5), VM_ANON, true);
  if (!spt_insert_page (&spt, page))
    fail ("aligned user page insert failed");
  msg ("aligned user page inserted");

  dup = new_uninit_page (unit_addr (5), VM_ANON, true);
  if (spt_insert_page (&spt, dup))
    fail ("duplicate page insert succeeded");
  vm_dealloc_page (dup);
  msg ("duplicates rejected");

  misaligned = new_uninit_page (unit_addr_ofs (6, 7), VM_ANON, true);
  if (spt_insert_page (&spt, misaligned))
    fail ("misaligned page insert succeeded");
  vm_dealloc_page (misaligned);

  kernel = new_uninit_page ((void *) KERN_BASE, VM_ANON, true);
  if (spt_insert_page (&spt, kernel))
    fail ("kernel address insert succeeded");
  vm_dealloc_page (kernel);

  low = new_uninit_page (NULL, VM_ANON, true);
  if (spt_insert_page (&spt, low))
    fail ("low address insert succeeded");
  vm_dealloc_page (low);
  msg ("invalid addresses rejected");

  supplemental_page_table_kill (&spt);
  pass ();
}

void
test_vm_spt_find (void)
{
  struct supplemental_page_table spt;
  struct page *page;

  init_current_spt ();
  init_spt (&spt);

  page = new_uninit_page (unit_addr (7), VM_ANON, true);
  if (!spt_insert_page (&spt, page))
    fail ("page insert failed");

  if (spt_find_page (&spt, unit_addr (7)) != page)
    fail ("exact lookup missed");
  msg ("exact lookup hit");

  if (spt_find_page (&spt, unit_addr_ofs (7, 123)) != page)
    fail ("offset lookup did not round down");
  msg ("offset lookup rounds down");

  if (spt_find_page (&spt, unit_addr (8)) != NULL)
    fail ("missing lookup returned a page");
  msg ("missing lookup null");

  supplemental_page_table_kill (&spt);
  pass ();
}

void
test_vm_spt_remove (void)
{
  struct supplemental_page_table spt;
  struct page *page;
  void *addr = unit_addr (9);

  init_current_spt ();
  init_spt (&spt);

  page = new_uninit_page (addr, VM_ANON, true);
  if (!spt_insert_page (&spt, page))
    fail ("page insert failed");

  spt_remove_page (&spt, page);
  if (spt_find_page (&spt, addr) != NULL)
    fail ("removed page was still findable");
  msg ("page removed");

  supplemental_page_table_kill (&spt);
  pass ();
}

void
test_vm_alloc_page (void)
{
  int aux = 42;
  void *anon_addr = unit_addr (10);
  void *file_addr = unit_addr (11);
  struct page *anon;
  struct page *file;

  init_current_spt ();

  if (!vm_alloc_page_with_initializer (VM_ANON | VM_MARKER_0, anon_addr,
                                       false, unit_initializer, &aux))
    fail ("anon vm_alloc_page_with_initializer failed");

  anon = spt_find_page (&thread_current ()->spt, anon_addr);
  if (anon == NULL || anon->operations->type != VM_UNINIT
      || page_get_type (anon) != VM_ANON || anon->writable)
    fail ("anon lazy page metadata was wrong");
  msg ("anon lazy page allocated");

  if (anon->uninit.init != unit_initializer || anon->uninit.aux != &aux)
    fail ("initializer metadata was not stored");
  msg ("initializer metadata stored");

  if (vm_alloc_page (VM_ANON, anon_addr, true))
    fail ("duplicate vm_alloc_page succeeded");
  msg ("duplicates rejected");

  if (!vm_alloc_page (VM_FILE, file_addr, true))
    fail ("file vm_alloc_page failed");
  file = spt_find_page (&thread_current ()->spt, file_addr);
  if (file == NULL || page_get_type (file) != VM_FILE || !file->writable)
    fail ("file lazy page metadata was wrong");
  msg ("file lazy page allocated");

  pass ();
}

void
test_vm_spt_copy (void)
{
  struct supplemental_page_table src;
  struct supplemental_page_table dst;
  struct page *anon;
  struct page *file;
  struct page *copied_anon;
  struct page *copied_file;

  init_current_spt ();
  init_spt (&src);
  init_spt (&dst);

  anon = new_uninit_page (unit_addr (12), VM_ANON, true);
  file = new_uninit_page (unit_addr (13), VM_FILE | VM_MARKER_1, false);
  if (!spt_insert_page (&src, anon) || !spt_insert_page (&src, file))
    fail ("source page insert failed");

  if (!supplemental_page_table_copy (&dst, &src))
    fail ("supplemental_page_table_copy returned false");
  msg ("uninit pages copied");

  copied_anon = spt_find_page (&dst, unit_addr (12));
  copied_file = spt_find_page (&dst, unit_addr (13));
  if (copied_anon == NULL || copied_file == NULL)
    fail ("copied pages were missing");
  if (copied_anon == anon || copied_file == file)
    fail ("copy reused source page objects");
  if (!copied_anon->writable || copied_file->writable
      || page_get_type (copied_anon) != VM_ANON
      || page_get_type (copied_file) != VM_FILE)
    fail ("copied page metadata was wrong");
  msg ("metadata preserved");

  supplemental_page_table_kill (&src);
  supplemental_page_table_kill (&dst);
  pass ();
}

void
test_vm_spt_kill (void)
{
  struct supplemental_page_table spt;

  init_current_spt ();
  init_spt (&spt);

  if (!spt_insert_page (&spt, new_uninit_page (unit_addr (14), VM_ANON, true))
      || !spt_insert_page (&spt, new_uninit_page (unit_addr (15), VM_FILE, false)))
    fail ("page insert failed");

  supplemental_page_table_kill (&spt);
  msg ("pages destroyed");

  init_spt (&spt);
  if (spt_find_page (&spt, unit_addr (14)) != NULL)
    fail ("reinitialized table found old page");
  msg ("table reusable");

  supplemental_page_table_kill (&spt);
  pass ();
}

void
test_vm_claim_page (void)
{
  void *addr = unit_addr (16);
  struct page *page;
  void *mapped;

  init_current_spt ();
  ensure_current_pml4 ();

  if (!vm_alloc_page (VM_ANON, addr, true))
    fail ("vm_alloc_page failed");
  msg ("lazy page allocated");

  page = spt_find_page (&thread_current ()->spt, addr);
  if (page == NULL)
    fail ("allocated page was missing");
  if (!vm_claim_page (addr))
    fail ("vm_claim_page returned false");

  mapped = pml4_get_page (thread_current ()->pml4, addr);
  if (page->frame == NULL || page->frame->kva == NULL
      || mapped != page->frame->kva || page_get_type (page) != VM_ANON)
    fail ("claimed page was not mapped to its frame");
  msg ("claim mapped frame");

  pass ();
}

void
test_vm_fault_invalid (void)
{
  void *ro_addr = unit_addr (17);

  init_current_spt ();

  if (vm_try_handle_fault (NULL, NULL, true, false, true))
    fail ("NULL fault address was accepted");
  if (vm_try_handle_fault (NULL, (void *) KERN_BASE, true, false, true))
    fail ("kernel fault address was accepted");
  if (vm_try_handle_fault (NULL, unit_addr (18), true, false, false))
    fail ("present-page fault was accepted");
  msg ("rejects invalid faults");

  if (!vm_alloc_page (VM_ANON, ro_addr, false))
    fail ("read-only vm_alloc_page failed");
  if (vm_try_handle_fault (NULL, ro_addr, true, true, true))
    fail ("write fault on read-only page was accepted");
  msg ("rejects write to read-only page");

  pass ();
}

void
test_vm_fault_claim (void)
{
  struct intr_frame f;
  void *addr = unit_addr (19);
  struct page *page;
  void *mapped;

  init_current_spt ();
  ensure_current_pml4 ();

  if (!vm_alloc_page (VM_ANON, addr, true))
    fail ("vm_alloc_page failed");
  msg ("lazy page allocated");

  f.rsp = (uint64_t) USER_STACK;
  if (!vm_try_handle_fault (&f, addr, true, false, true))
    fail ("vm_try_handle_fault returned false");

  page = spt_find_page (&thread_current ()->spt, addr);
  mapped = pml4_get_page (thread_current ()->pml4, addr);
  if (page == NULL || page->frame == NULL || mapped != page->frame->kva)
    fail ("fault did not claim and map the page");
  msg ("fault claimed page");

  pass ();
}
