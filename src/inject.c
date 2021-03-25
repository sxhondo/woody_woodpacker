#include "woody-woodpacker.h"

Elf64_Shdr    *
find_section(void *data, char *name)
{
   Elf64_Ehdr *elf_hdr = (Elf64_Ehdr *)data;
   Elf64_Shdr *shdr = (Elf64_Shdr *)(data + elf_hdr->e_shoff);
   Elf64_Shdr *sh_strtab = &shdr[elf_hdr->e_shstrndx];
   const char *const sh_strtab_p = data + sh_strtab->sh_offset;

   printf("+ %d section in file. Looking for section '%s'\n",
      elf_hdr->e_shnum, name);

   for (int i = 0; i < elf_hdr->e_shnum; i++)
   {
      char *sname = (char *)(sh_strtab_p + shdr[i].sh_name);
      if (!strcmp(sname, name)) return &shdr[i];
   }
   printf("- Could not find '%s'\n", name);
   return NULL;
}

static Elf64_Phdr *
find_padding_area(void *data, int *cc_offset, int psize)
{
   Elf64_Ehdr *elf_hdr = (Elf64_Ehdr *)data;
   Elf64_Phdr *curr_seg, *load_seg;

   curr_seg = (Elf64_Phdr *)((unsigned char *)elf_hdr
         + (unsigned int) elf_hdr->e_phoff);

   for (int i = 0; i < elf_hdr->e_phnum; i++)
   {
      if (curr_seg->p_type == PT_LOAD && curr_seg->p_flags & PF_X)
      {
         curr_seg->p_flags |= PF_W;
         printf("   * Found executable LOAD segment (#%d)\n", i);
         load_seg = curr_seg;
         break ;
      }
      curr_seg = (Elf64_Phdr *) ((unsigned char *)curr_seg 
            + (unsigned int) elf_hdr->e_phentsize);         
   }

   int seg_size = 0;
   while (seg_size < curr_seg->p_filesz)
      seg_size += curr_seg->p_align;

   printf("   * Segment size: %d\n", seg_size);

   int count = 0;
   void *t_seg_ptr = data + curr_seg->p_offset;
   for (int i = 0; i < seg_size; i++)
   {
      if (*(char *)t_seg_ptr == 0x00)
      {
         if (count == 0)
            *cc_offset = curr_seg->p_offset + i;
         count++;
         // if (count == psize) break;
      }
      else count = 0;
      t_seg_ptr++;
   }
   printf("+ Codecave at offset 0x%x (%d bytes)\n", *cc_offset, count);
   printf("   * Needed (%d bytes)\n", psize);
   return load_seg;
}

Elf64_Addr     
find_virtual_address(void *data)
{
   Elf64_Ehdr  *elf_hdr = (Elf64_Ehdr *)data;
   Elf64_Phdr  *curr_seg;

   curr_seg = (Elf64_Phdr *)((unsigned char *)elf_hdr 
               + (unsigned int) elf_hdr->e_phoff);

   for (int i = 0; i < elf_hdr->e_shnum; i++)
   {
      if (curr_seg->p_type == PT_LOAD)
      {
         printf("+ Base address (p_vaddr) is %p\n", (void *)curr_seg->p_vaddr);
         return curr_seg->p_vaddr;
      }
      curr_seg = (Elf64_Phdr *) ((unsigned char *)curr_seg 
            + (unsigned int) elf_hdr->e_phentsize);
   }

   printf("- PT_LOAD segment not found\n");
   return (Elf64_Addr)0x0;
}

void     
inject_payload(void *target, void *payload, int tsize, int psize)
{
   Elf64_Ehdr     *t_ehdr = ((Elf64_Ehdr *)target);
   int            cc_offset;

   Elf64_Addr ep = t_ehdr->e_entry;
   printf ("+ Original entry point: %p\n", (void *)ep);

   Elf64_Shdr *p_text_sec = find_section(payload, ".text");
   Elf64_Phdr *t_load_seg = find_padding_area(target, &cc_offset, p_text_sec->sh_size);
   Elf64_Addr base = find_virtual_address(target);
  
   memmove(target + cc_offset, payload + p_text_sec->sh_offset, p_text_sec->sh_size);
   
   t_load_seg->p_filesz += p_text_sec->sh_size;
   t_load_seg->p_memsz += p_text_sec->sh_size;

   /* Patch return address */
   insert_jmp_to_ep(target + cc_offset, p_text_sec->sh_size, (long)ep);

   t_ehdr->e_entry = (Elf64_Addr)(base + cc_offset);
   printf("+ e_entry set to: %lx\n", t_ehdr->e_entry);
}