#include "debug_dwarf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

/* Lightweight DWARF 2-4 reader aimed at AVR-GCC output.  It intentionally
** keeps the supported evaluator small and deterministic.  Unsupported
** location expressions remain visible to the debugger instead of being
** guessed. */

#define DW_TAG_array_type        0x01U
#define DW_TAG_enumeration_type  0x04U
#define DW_TAG_formal_parameter  0x05U
#define DW_TAG_lexical_block     0x0BU
#define DW_TAG_member            0x0DU
#define DW_TAG_pointer_type      0x0FU
#define DW_TAG_compile_unit      0x11U
#define DW_TAG_structure_type    0x13U
#define DW_TAG_typedef           0x16U
#define DW_TAG_union_type        0x17U
#define DW_TAG_subrange_type     0x21U
#define DW_TAG_base_type         0x24U
#define DW_TAG_const_type        0x26U
#define DW_TAG_enumerator        0x28U
#define DW_TAG_subprogram        0x2EU
#define DW_TAG_variable          0x34U
#define DW_TAG_volatile_type     0x35U

#define DW_AT_location           0x02U
#define DW_AT_name               0x03U
#define DW_AT_byte_size          0x0BU
#define DW_AT_bit_offset         0x0CU
#define DW_AT_bit_size           0x0DU
#define DW_AT_low_pc             0x11U
#define DW_AT_high_pc            0x12U
#define DW_AT_const_value        0x1CU
#define DW_AT_upper_bound        0x2FU
#define DW_AT_abstract_origin    0x31U
#define DW_AT_count              0x37U
#define DW_AT_data_member_location 0x38U
#define DW_AT_decl_file          0x3AU
#define DW_AT_decl_line          0x3BU
#define DW_AT_encoding           0x3EU
#define DW_AT_external           0x3FU
#define DW_AT_frame_base         0x40U
#define DW_AT_specification      0x47U
#define DW_AT_type               0x49U

#define DW_FORM_addr             0x01U
#define DW_FORM_block2           0x03U
#define DW_FORM_block4           0x04U
#define DW_FORM_data2            0x05U
#define DW_FORM_data4            0x06U
#define DW_FORM_data8            0x07U
#define DW_FORM_string           0x08U
#define DW_FORM_block            0x09U
#define DW_FORM_block1           0x0AU
#define DW_FORM_data1            0x0BU
#define DW_FORM_flag             0x0CU
#define DW_FORM_sdata            0x0DU
#define DW_FORM_strp             0x0EU
#define DW_FORM_udata            0x0FU
#define DW_FORM_ref_addr         0x10U
#define DW_FORM_ref1             0x11U
#define DW_FORM_ref2             0x12U
#define DW_FORM_ref4             0x13U
#define DW_FORM_ref8             0x14U
#define DW_FORM_ref_udata        0x15U
#define DW_FORM_indirect         0x16U
#define DW_FORM_sec_offset       0x17U
#define DW_FORM_exprloc          0x18U
#define DW_FORM_flag_present     0x19U

#define DW_OP_addr               0x03U
#define DW_OP_deref              0x06U
#define DW_OP_const1u            0x08U
#define DW_OP_const1s            0x09U
#define DW_OP_const2u            0x0AU
#define DW_OP_const2s            0x0BU
#define DW_OP_const4u            0x0CU
#define DW_OP_const4s            0x0DU
#define DW_OP_constu             0x10U
#define DW_OP_consts             0x11U
#define DW_OP_dup                0x12U
#define DW_OP_drop               0x13U
#define DW_OP_over               0x14U
#define DW_OP_swap               0x16U
#define DW_OP_minus              0x1CU
#define DW_OP_mul                0x1EU
#define DW_OP_and                0x1AU
#define DW_OP_or                 0x21U
#define DW_OP_plus               0x22U
#define DW_OP_plus_uconst        0x23U
#define DW_OP_shl                0x24U
#define DW_OP_shr                0x25U
#define DW_OP_xor                0x27U
#define DW_OP_lit0               0x30U
#define DW_OP_reg0               0x50U
#define DW_OP_breg0              0x70U
#define DW_OP_regx               0x90U
#define DW_OP_fbreg              0x91U
#define DW_OP_bregx              0x92U
#define DW_OP_piece              0x93U
#define DW_OP_deref_size         0x94U
#define DW_OP_nop                0x96U
#define DW_OP_stack_value        0x9FU

#define EXPR_MAX 96U
#define STACK_MAX 16U
#define SCOPE_MAX 64U
#define ATTR_MAX 40U
#define STATUS_MAX 256U

typedef struct{
    uint8_t* bytes;
    uint32_t len;
    uint32_t loclist_off;
    uint8_t is_loclist;
} expr_t;

typedef struct{
    uint32_t die_off;
    uint32_t kind;
    uint32_t byte_size;
    uint32_t target_die;
    uint32_t target_type;
    uint32_t element_count;
    uint32_t first_member;
    uint32_t member_count;
    uint32_t encoding;
    uint32_t origin_die;
    char* name;
} type_t;

typedef struct{
    uint32_t owner_type;
    uint32_t type_die;
    uint32_t type_id;
    uint32_t byte_offset;
    int32_t bit_offset;
    uint32_t bit_size;
    uint8_t is_enumerator;
    int64_t const_value;
    char* name;
} member_t;

typedef struct{
    uint32_t die_off;
    uint32_t scope;
    uint32_t type_die;
    uint32_t type_id;
    uint32_t file_index;
    uint32_t line;
    uint32_t low_word;
    uint32_t high_word;
    uint32_t function_id;
    uint32_t origin_die;
    char* name;
    expr_t loc;
} variable_t;

typedef struct{
    uint32_t die_off;
    uint32_t low_word;
    uint32_t high_word;
    uint32_t origin_die;
    char* name;
    expr_t frame_base;
} function_t;

typedef struct{
    uint32_t tag;
    uint32_t type_id;
    uint32_t function_id;
    uint32_t low_word;
    uint32_t high_word;
} scope_t;

typedef struct{
    uint32_t attr;
    uint32_t form;
    uint64_t u;
    int64_t s;
    char const* str;
    uint8_t const* block;
    uint32_t block_len;
} attr_t;

typedef struct{
    uint8_t* file_data;
    size_t file_size;
    uint8_t const* info; uint32_t info_size;
    uint8_t const* abbrev; uint32_t abbrev_size;
    uint8_t const* str; uint32_t str_size;
    uint8_t const* loc; uint32_t loc_size;
} elf_sections_t;

static type_t* g_types; static uint32_t g_type_count, g_type_cap;
static member_t* g_members; static uint32_t g_member_count, g_member_cap;
static variable_t* g_vars; static uint32_t g_var_count, g_var_cap;
static function_t* g_funcs; static uint32_t g_func_count, g_func_cap;
static uint8_t* g_loc_data; static uint32_t g_loc_size;
static uint8_t g_addr_size = 4U;
static char g_status[STATUS_MAX] = "No DWARF variables/types loaded.";

static uint16_t rd16(uint8_t const* p){ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t rd32(uint8_t const* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(uint8_t const* p){ return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4U) << 32); }

static void set_status(char const* fmt, ...){ va_list ap; va_start(ap,fmt); vsnprintf(g_status,sizeof(g_status),fmt,ap); va_end(ap); }
static char* dstr(char const* s){ size_t n; char* p; if(!s)return NULL; n=strlen(s)+1U; p=(char*)malloc(n); if(p)memcpy(p,s,n); return p; }
static int grow(void** p,uint32_t* cap,uint32_t count,size_t esz,uint32_t initial){ void* n; uint32_t nc; if(count<*cap)return 1; nc=*cap?(*cap*2U):initial; n=realloc(*p,(size_t)nc*esz); if(!n)return 0; *p=n; *cap=nc; return 1; }
static void expr_free(expr_t* e){ if(e){ free(e->bytes); memset(e,0,sizeof(*e)); } }
static int expr_copy(expr_t* e,uint8_t const* b,uint32_t len){ expr_free(e); if(!b||!len)return 1; if(len>EXPR_MAX)len=EXPR_MAX; e->bytes=(uint8_t*)malloc(len); if(!e->bytes)return 0; memcpy(e->bytes,b,len); e->len=len; return 1; }

static int uleb(uint8_t const** pp,uint8_t const* end,uint64_t* out){ uint64_t v=0; unsigned sh=0; uint8_t const* p=*pp; while(p<end&&sh<64U){ uint8_t b=*p++; v|=((uint64_t)(b&0x7FU))<<sh; if(!(b&0x80U)){*pp=p;if(out)*out=v;return 1;} sh+=7U;} return 0; }
static int sleb(uint8_t const** pp,uint8_t const* end,int64_t* out){ int64_t v=0; unsigned sh=0; uint8_t b=0; uint8_t const* p=*pp; do{ if(p>=end||sh>=64U)return 0; b=*p++; v|=((int64_t)(b&0x7FU))<<sh; sh+=7U; }while(b&0x80U); if(sh<64U&&(b&0x40U))v|=-((int64_t)1<<sh); *pp=p;if(out)*out=v;return 1; }
static uint64_t rd_addr(uint8_t const* p,uint8_t n){ if(n==1)return p[0]; if(n==2)return rd16(p); if(n==4)return rd32(p); if(n==8)return rd64(p); return 0; }

static int load_sections(char const* path,elf_sections_t* s){ FILE* f; long fl; uint8_t* d; uint32_t shoff; uint16_t shentsize,shnum,shstrndx,i; memset(s,0,sizeof(*s)); f=fopen(path,"rb"); if(!f)return 0; if(fseek(f,0,SEEK_END)||((fl=ftell(f))<0)||fseek(f,0,SEEK_SET)){fclose(f);return 0;} d=(uint8_t*)malloc((size_t)fl? (size_t)fl:1U); if(!d){fclose(f);return 0;} if((size_t)fl!=fread(d,1,(size_t)fl,f)){fclose(f);free(d);return 0;} fclose(f); s->file_data=d;s->file_size=(size_t)fl; if(s->file_size<52U||memcmp(d,"\x7f""ELF",4)||d[4]!=1U||d[5]!=1U)return 0; shoff=rd32(d+32);shentsize=rd16(d+46);shnum=rd16(d+48);shstrndx=rd16(d+50); if(shentsize<40U||!shnum||shstrndx>=shnum||(size_t)shoff+(size_t)shentsize*shnum>s->file_size)return 0; { uint8_t const* shs=d+shoff+(size_t)shstrndx*shentsize; uint32_t so=rd32(shs+16),ss=rd32(shs+20); if((size_t)so+ss>s->file_size)return 0; for(i=0;i<shnum;i++){ uint8_t const* sh=d+shoff+(size_t)i*shentsize; uint32_t no=rd32(sh),off=rd32(sh+16),sz=rd32(sh+20); char const* n; if(no>=ss||(size_t)off+sz>s->file_size)continue; n=(char const*)(d+so+no); if(!strcmp(n,".debug_info")){s->info=d+off;s->info_size=sz;} else if(!strcmp(n,".debug_abbrev")){s->abbrev=d+off;s->abbrev_size=sz;} else if(!strcmp(n,".debug_str")){s->str=d+off;s->str_size=sz;} else if(!strcmp(n,".debug_loc")){s->loc=d+off;s->loc_size=sz;} } } return s->info&&s->abbrev; }
static void free_sections(elf_sections_t* s){ free(s->file_data); memset(s,0,sizeof(*s)); }

static int abbrev_find(elf_sections_t const* es,uint32_t off,uint64_t want,uint32_t* tag,uint8_t* children,uint8_t const** attrs,uint8_t const** attrs_end){ uint8_t const* p; uint8_t const* end; if(off>=es->abbrev_size)return 0; p=es->abbrev+off;end=es->abbrev+es->abbrev_size; while(p<end){ uint64_t code,t; uint8_t ch; uint8_t const* a; if(!uleb(&p,end,&code))return 0; if(code==0)continue; if(!uleb(&p,end,&t)||p>=end)return 0; ch=*p++;a=p; while(p<end){uint64_t an,fo;if(!uleb(&p,end,&an)||!uleb(&p,end,&fo))return 0;if(an==0&&fo==0)break;} if(code==want){*tag=(uint32_t)t;*children=ch;*attrs=a;*attrs_end=p;return 1;} } return 0; }

static int parse_form(uint32_t form,uint8_t const** pp,uint8_t const* end,uint8_t addr_size,uint32_t cu_start,elf_sections_t const* es,attr_t* a){ uint8_t const* p=*pp; uint64_t u=0; int64_t sv=0; uint32_t n=0; if(form==DW_FORM_indirect){ if(!uleb(&p,end,&u))return 0; a->form=(uint32_t)u; *pp=p; return parse_form((uint32_t)u,pp,end,addr_size,cu_start,es,a); } switch(form){ case DW_FORM_addr: if((size_t)(end-p)<addr_size)return 0;a->u=rd_addr(p,addr_size);p+=addr_size;break; case DW_FORM_data1:case DW_FORM_flag:case DW_FORM_ref1: if(p>=end)return 0;a->u=*p++;break; case DW_FORM_data2:case DW_FORM_ref2: if((size_t)(end-p)<2)return 0;a->u=rd16(p);p+=2;break; case DW_FORM_data4:case DW_FORM_ref4:case DW_FORM_sec_offset:case DW_FORM_ref_addr: if((size_t)(end-p)<4)return 0;a->u=rd32(p);p+=4;break; case DW_FORM_data8:case DW_FORM_ref8: if((size_t)(end-p)<8)return 0;a->u=rd64(p);p+=8;break; case DW_FORM_udata:case DW_FORM_ref_udata: if(!uleb(&p,end,&u))return 0;a->u=u;break; case DW_FORM_sdata: if(!sleb(&p,end,&sv))return 0;a->s=sv;a->u=(uint64_t)sv;break; case DW_FORM_string: {uint8_t const* q=p;while(q<end&&*q)q++;if(q>=end)return 0;a->str=(char const*)p;p=q+1;break;} case DW_FORM_strp: if((size_t)(end-p)<4)return 0;a->u=rd32(p);p+=4;break; case DW_FORM_block1: if(p>=end)return 0;n=*p++;goto block_common; case DW_FORM_block2: if((size_t)(end-p)<2)return 0;n=rd16(p);p+=2;goto block_common; case DW_FORM_block4: if((size_t)(end-p)<4)return 0;n=rd32(p);p+=4;goto block_common; case DW_FORM_block:case DW_FORM_exprloc: if(!uleb(&p,end,&u))return 0;n=(uint32_t)u; block_common: if((size_t)(end-p)<n)return 0;a->block=p;a->block_len=n;p+=n;break; case DW_FORM_flag_present: a->u=1;break; default:return 0; }
 if(form==DW_FORM_strp){ if(a->u<es->str_size)a->str=(char const*)(es->str+(uint32_t)a->u); }
 if(form==DW_FORM_ref1||form==DW_FORM_ref2||form==DW_FORM_ref4||form==DW_FORM_ref8||form==DW_FORM_ref_udata)a->u += cu_start;
 *pp=p; return 1; }

static attr_t const* find_attr(attr_t const* a,uint32_t n,uint32_t key){ uint32_t i;for(i=0;i<n;i++)if(a[i].attr==key)return &a[i];return NULL; }
static uint32_t addr_to_word(uint64_t a){ return (uint32_t)((a>>1)&0x7FFFU); }

static uint32_t type_add(uint32_t die,uint32_t kind,char const* name){ type_t* t;if(!grow((void**)&g_types,&g_type_cap,g_type_count,sizeof(*g_types),64))return UINT32_MAX;t=&g_types[g_type_count];memset(t,0,sizeof(*t));t->die_off=die;t->kind=kind;t->target_type=UINT32_MAX;t->target_die=UINT32_MAX;t->first_member=g_member_count;t->name=dstr(name?name:"");return g_type_count++; }
static uint32_t func_add(uint32_t die,char const* name){ function_t* f;if(!grow((void**)&g_funcs,&g_func_cap,g_func_count,sizeof(*g_funcs),32))return UINT32_MAX;f=&g_funcs[g_func_count];memset(f,0,sizeof(*f));f->die_off=die;f->origin_die=UINT32_MAX;f->name=dstr(name?name:"");return g_func_count++; }
static uint32_t var_add(void){ variable_t* v;if(!grow((void**)&g_vars,&g_var_cap,g_var_count,sizeof(*g_vars),128))return UINT32_MAX;v=&g_vars[g_var_count];memset(v,0,sizeof(*v));v->type_id=UINT32_MAX;v->type_die=UINT32_MAX;v->file_index=UINT32_MAX;v->function_id=UINT32_MAX;v->origin_die=UINT32_MAX;return g_var_count++; }
static uint32_t member_add(uint32_t owner){ member_t* m;if(!grow((void**)&g_members,&g_member_cap,g_member_count,sizeof(*g_members),128))return UINT32_MAX;m=&g_members[g_member_count];memset(m,0,sizeof(*m));m->owner_type=owner;m->type_id=UINT32_MAX;m->type_die=UINT32_MAX;m->bit_offset=-1;return g_member_count++; }
static uint32_t type_by_die(uint32_t off){uint32_t i;for(i=0;i<g_type_count;i++)if(g_types[i].die_off==off)return i;return UINT32_MAX;}
static uint32_t func_by_die(uint32_t off){uint32_t i;for(i=0;i<g_func_count;i++)if(g_funcs[i].die_off==off)return i;return UINT32_MAX;}
static variable_t* var_by_die(uint32_t off){uint32_t i;for(i=0;i<g_var_count;i++)if(g_vars[i].die_off==off)return &g_vars[i];return NULL;}
static uint32_t simple_member_offset(attr_t const* a){ uint8_t const* p;uint8_t const* e;uint64_t u;if(!a)return 0;if(a->block&&a->block_len){p=a->block;e=p+a->block_len;if(p<e&&*p++==DW_OP_plus_uconst&&uleb(&p,e,&u))return (uint32_t)u;return 0;}return (uint32_t)a->u; }

static int parse_info_unit(elf_sections_t const* es,uint8_t const* cu,uint8_t const* cu_end,uint32_t cu_start,uint16_t ver,uint32_t abbrev_off,uint8_t addr_size){ uint8_t const* p=cu;scope_t stack[SCOPE_MAX];uint32_t depth=0; (void)ver; while(p<cu_end){ uint8_t const* die_start=p;uint64_t code;uint32_t tag;uint8_t children;uint8_t const* ap;uint8_t const* ae;attr_t av[ATTR_MAX];uint32_t ac=0;uint32_t cur_type=UINT32_MAX,cur_func=UINT32_MAX; if(!uleb(&p,cu_end,&code))return 0;if(code==0){if(depth)depth--;continue;} if(!abbrev_find(es,abbrev_off,code,&tag,&children,&ap,&ae))return 0; while(ap<ae){uint64_t an,fo;attr_t a;if(!uleb(&ap,ae,&an)||!uleb(&ap,ae,&fo))return 0;if(an==0&&fo==0)break;memset(&a,0,sizeof(a));a.attr=(uint32_t)an;a.form=(uint32_t)fo;if(!parse_form((uint32_t)fo,&p,cu_end,addr_size,cu_start,es,&a))return 0;if(ac<ATTR_MAX)av[ac++]=a;}
 { attr_t const* n=find_attr(av,ac,DW_AT_name);attr_t const* bs=find_attr(av,ac,DW_AT_byte_size);attr_t const* ty=find_attr(av,ac,DW_AT_type);attr_t const* lo=find_attr(av,ac,DW_AT_low_pc);attr_t const* hi=find_attr(av,ac,DW_AT_high_pc);attr_t const* org=find_attr(av,ac,DW_AT_abstract_origin);attr_t const* spec=find_attr(av,ac,DW_AT_specification); if(tag==DW_TAG_base_type||tag==DW_TAG_pointer_type||tag==DW_TAG_typedef||tag==DW_TAG_const_type||tag==DW_TAG_volatile_type||tag==DW_TAG_array_type||tag==DW_TAG_structure_type||tag==DW_TAG_union_type||tag==DW_TAG_enumeration_type){ uint32_t kind=CU_DBG_DWARF_TYPE_UNKNOWN; type_t* t; if(tag==DW_TAG_base_type)kind=CU_DBG_DWARF_TYPE_BASE;else if(tag==DW_TAG_pointer_type)kind=CU_DBG_DWARF_TYPE_POINTER;else if(tag==DW_TAG_typedef)kind=CU_DBG_DWARF_TYPE_TYPEDEF;else if(tag==DW_TAG_const_type)kind=CU_DBG_DWARF_TYPE_CONST;else if(tag==DW_TAG_volatile_type)kind=CU_DBG_DWARF_TYPE_VOLATILE;else if(tag==DW_TAG_array_type)kind=CU_DBG_DWARF_TYPE_ARRAY;else if(tag==DW_TAG_structure_type)kind=CU_DBG_DWARF_TYPE_STRUCT;else if(tag==DW_TAG_union_type)kind=CU_DBG_DWARF_TYPE_UNION;else if(tag==DW_TAG_enumeration_type)kind=CU_DBG_DWARF_TYPE_ENUM; cur_type=type_add((uint32_t)(die_start-es->info),kind,n?n->str:"");if(cur_type==UINT32_MAX)return 0;t=&g_types[cur_type];if(bs)t->byte_size=(uint32_t)bs->u;if(ty)t->target_die=(uint32_t)ty->u;{attr_t const* en=find_attr(av,ac,DW_AT_encoding);if(en)t->encoding=(uint32_t)en->u;}if(org)t->origin_die=(uint32_t)org->u;else if(spec)t->origin_die=(uint32_t)spec->u; }
 else if(tag==DW_TAG_subprogram){ function_t* f;cur_func=func_add((uint32_t)(die_start-es->info),n?n->str:"");if(cur_func==UINT32_MAX)return 0;f=&g_funcs[cur_func];if(lo)f->low_word=addr_to_word(lo->u);if(hi){if(hi->form==DW_FORM_addr)f->high_word=addr_to_word(hi->u);else f->high_word=f->low_word+(uint32_t)((hi->u+1U)>>1);}if(org)f->origin_die=(uint32_t)org->u;else if(spec)f->origin_die=(uint32_t)spec->u;{attr_t const* fb=find_attr(av,ac,DW_AT_frame_base);if(fb){if(fb->block)expr_copy(&f->frame_base,fb->block,fb->block_len);else if(fb->form==DW_FORM_data4||fb->form==DW_FORM_sec_offset){f->frame_base.is_loclist=1;f->frame_base.loclist_off=(uint32_t)fb->u;}}} }
 else if(tag==DW_TAG_variable||tag==DW_TAG_formal_parameter){ uint32_t id=var_add();variable_t* v;if(id==UINT32_MAX)return 0;v=&g_vars[id];v->die_off=(uint32_t)(die_start-es->info);v->name=dstr(n?n->str:"");if(ty)v->type_die=(uint32_t)ty->u;if(org)v->origin_die=(uint32_t)org->u;else if(spec)v->origin_die=(uint32_t)spec->u;{attr_t const* df=find_attr(av,ac,DW_AT_decl_file);attr_t const* dl=find_attr(av,ac,DW_AT_decl_line);if(df&&df->u)v->file_index=(uint32_t)(df->u-1U);if(dl)v->line=(uint32_t)dl->u;} {int si;uint32_t fn=UINT32_MAX,lw=0,hw=0;for(si=(int)depth-1;si>=0;--si){if((!lw&&!hw)&&(stack[si].low_word||stack[si].high_word)){lw=stack[si].low_word;hw=stack[si].high_word;}if(stack[si].tag==DW_TAG_subprogram){fn=stack[si].function_id;if(!lw&&!hw){lw=stack[si].low_word;hw=stack[si].high_word;}break;}}v->function_id=fn;v->low_word=lw;v->high_word=hw;v->scope=(tag==DW_TAG_formal_parameter)?CU_DBG_DWARF_SCOPE_PARAM:((fn!=UINT32_MAX)?CU_DBG_DWARF_SCOPE_LOCAL:CU_DBG_DWARF_SCOPE_GLOBAL);} {attr_t const* l=find_attr(av,ac,DW_AT_location);if(l){if(l->block)expr_copy(&v->loc,l->block,l->block_len);else if(l->form==DW_FORM_data4||l->form==DW_FORM_sec_offset){v->loc.is_loclist=1;v->loc.loclist_off=(uint32_t)l->u;}}} }
 else if(tag==DW_TAG_member){int si;uint32_t owner=UINT32_MAX;for(si=(int)depth-1;si>=0;--si)if(stack[si].type_id!=UINT32_MAX){owner=stack[si].type_id;break;}if(owner!=UINT32_MAX){uint32_t mi=member_add(owner);member_t* m;if(mi==UINT32_MAX)return 0;m=&g_members[mi];m->name=dstr(n?n->str:"");if(ty)m->type_die=(uint32_t)ty->u;{attr_t const* d=find_attr(av,ac,DW_AT_data_member_location);m->byte_offset=simple_member_offset(d);} {attr_t const* bo=find_attr(av,ac,DW_AT_bit_offset);attr_t const* bz=find_attr(av,ac,DW_AT_bit_size);if(bo)m->bit_offset=(int32_t)bo->u;if(bz)m->bit_size=(uint32_t)bz->u;}g_types[owner].member_count++;} }
 else if(tag==DW_TAG_enumerator){int si;uint32_t owner=UINT32_MAX;for(si=(int)depth-1;si>=0;--si)if(stack[si].type_id!=UINT32_MAX&&g_types[stack[si].type_id].kind==CU_DBG_DWARF_TYPE_ENUM){owner=stack[si].type_id;break;}if(owner!=UINT32_MAX){uint32_t mi=member_add(owner);member_t* m;if(mi==UINT32_MAX)return 0;m=&g_members[mi];m->name=dstr(n?n->str:"");m->is_enumerator=1;{attr_t const* cv=find_attr(av,ac,DW_AT_const_value);if(cv)m->const_value=(cv->form==DW_FORM_sdata)?cv->s:(int64_t)cv->u;}g_types[owner].member_count++;} }
 else if(tag==DW_TAG_subrange_type){int si;uint32_t owner=UINT32_MAX;for(si=(int)depth-1;si>=0;--si)if(stack[si].type_id!=UINT32_MAX&&g_types[stack[si].type_id].kind==CU_DBG_DWARF_TYPE_ARRAY){owner=stack[si].type_id;break;}if(owner!=UINT32_MAX){attr_t const* c=find_attr(av,ac,DW_AT_count);attr_t const* ub=find_attr(av,ac,DW_AT_upper_bound);if(c)g_types[owner].element_count=(uint32_t)c->u;else if(ub)g_types[owner].element_count=(uint32_t)ub->u+1U;} }
 }
 if(children){ scope_t s;attr_t const* lo=find_attr(av,ac,DW_AT_low_pc);attr_t const* hi=find_attr(av,ac,DW_AT_high_pc);memset(&s,0,sizeof(s));s.tag=tag;s.type_id=cur_type;s.function_id=cur_func;if(cur_func!=UINT32_MAX){s.low_word=g_funcs[cur_func].low_word;s.high_word=g_funcs[cur_func].high_word;}else{if(lo)s.low_word=addr_to_word(lo->u);if(hi){if(hi->form==DW_FORM_addr)s.high_word=addr_to_word(hi->u);else s.high_word=s.low_word+(uint32_t)((hi->u+1U)>>1);}}if(depth<SCOPE_MAX)stack[depth++]=s;}
 }
 return 1; }

static void resolve_refs(void)
{
    uint32_t i, j;
    /* First resolve all direct references so abstract-origin/specification
    ** inheritance never depends on DIE order. */
    for(i=0;i<g_type_count;i++){
        if(g_types[i].target_die!=UINT32_MAX) g_types[i].target_type=type_by_die(g_types[i].target_die);
        if(!g_types[i].byte_size&&g_types[i].kind==CU_DBG_DWARF_TYPE_POINTER) g_types[i].byte_size=2U;
    }
    for(i=0;i<g_member_count;i++) g_members[i].type_id=type_by_die(g_members[i].type_die);
    for(i=0;i<g_var_count;i++) g_vars[i].type_id=type_by_die(g_vars[i].type_die);

    for(i=0;i<g_type_count;i++){
        if(g_types[i].origin_die!=UINT32_MAX){
            uint32_t oi=type_by_die(g_types[i].origin_die);
            if(oi!=UINT32_MAX){
                type_t const* o=&g_types[oi];
                if((!g_types[i].name||!g_types[i].name[0])&&o->name){free(g_types[i].name);g_types[i].name=dstr(o->name);}
                if(g_types[i].target_type==UINT32_MAX)g_types[i].target_type=o->target_type;
                if(!g_types[i].byte_size)g_types[i].byte_size=o->byte_size;
                if(!g_types[i].encoding)g_types[i].encoding=o->encoding;
            }
        }
    }
    for(i=0;i<g_var_count;i++){
        if(g_vars[i].origin_die!=UINT32_MAX){
            variable_t* o=var_by_die(g_vars[i].origin_die);
            if(o){
                if((!g_vars[i].name||!g_vars[i].name[0])&&o->name){free(g_vars[i].name);g_vars[i].name=dstr(o->name);}
                if(g_vars[i].type_id==UINT32_MAX)g_vars[i].type_id=o->type_id;
                if(g_vars[i].file_index==UINT32_MAX)g_vars[i].file_index=o->file_index;
                if(!g_vars[i].line)g_vars[i].line=o->line;
            }
        }
    }
    for(i=0;i<g_func_count;i++){
        if(g_funcs[i].origin_die!=UINT32_MAX){
            uint32_t fi=func_by_die(g_funcs[i].origin_die);
            if(fi!=UINT32_MAX){
                function_t const* o=&g_funcs[fi];
                if((!g_funcs[i].name||!g_funcs[i].name[0])&&o->name){free(g_funcs[i].name);g_funcs[i].name=dstr(o->name);}
                if(!g_funcs[i].low_word&&!g_funcs[i].high_word){g_funcs[i].low_word=o->low_word;g_funcs[i].high_word=o->high_word;}
            }
        }
    }
    /* Member lookup is owner based, so this merely records convenient counts. */
    for(i=0;i<g_type_count;i++){
        uint32_t first=UINT32_MAX,count=0;
        for(j=0;j<g_member_count;j++)if(g_members[j].owner_type==i){if(first==UINT32_MAX)first=j;count++;}
        g_types[i].first_member=(first==UINT32_MAX)?0:first;
        g_types[i].member_count=count;
    }
}


void cu_debug_dwarf_reset(void){uint32_t i;for(i=0;i<g_type_count;i++)free(g_types[i].name);for(i=0;i<g_member_count;i++)free(g_members[i].name);for(i=0;i<g_var_count;i++){free(g_vars[i].name);expr_free(&g_vars[i].loc);}for(i=0;i<g_func_count;i++){free(g_funcs[i].name);expr_free(&g_funcs[i].frame_base);}free(g_types);free(g_members);free(g_vars);free(g_funcs);free(g_loc_data);g_types=NULL;g_members=NULL;g_vars=NULL;g_funcs=NULL;g_loc_data=NULL;g_type_count=g_member_count=g_var_count=g_func_count=0;g_type_cap=g_member_cap=g_var_cap=g_func_cap=0;g_loc_size=0;g_addr_size=4U;set_status("No DWARF variables/types loaded.");}

int cu_debug_dwarf_load_elf(char const* path){elf_sections_t es;uint8_t const* p;uint8_t const* end;uint32_t units=0,bad=0;cu_debug_dwarf_reset();if(!load_sections(path,&es)){set_status("ELF has no usable .debug_info/.debug_abbrev sections.");free_sections(&es);return 0;}if(es.loc&&es.loc_size){g_loc_data=(uint8_t*)malloc(es.loc_size);if(g_loc_data){memcpy(g_loc_data,es.loc,es.loc_size);g_loc_size=es.loc_size;}}p=es.info;end=es.info+es.info_size;while((size_t)(end-p)>=11U){uint8_t const* unit0=p;uint32_t len=rd32(p);uint8_t const* ue;uint16_t ver;uint32_t ao;uint8_t as;p+=4;if(!len)continue;if(len==0xFFFFFFFFU||(size_t)(end-p)<len){bad++;break;}ue=p+len;if((size_t)(ue-p)<7U){bad++;p=ue;continue;}ver=rd16(p);p+=2;ao=rd32(p);p+=4;as=*p++;if(ver<2U||ver>4U||!(as==2U||as==4U||as==8U)){bad++;p=ue;continue;}g_addr_size=as;if(parse_info_unit(&es,p,ue,(uint32_t)(unit0-es.info),ver,ao,as))units++;else bad++;p=ue;}free_sections(&es);resolve_refs();if(!g_type_count&&!g_var_count){set_status("DWARF info parsed but contained no supported variables/types%s",bad?" (some units unsupported)":"");return 0;}set_status("Loaded %u DWARF type%s, %u variable%s and %u function%s (%u unit%s%s)",(unsigned)g_type_count,g_type_count==1?"":"s",(unsigned)g_var_count,g_var_count==1?"":"s",(unsigned)g_func_count,g_func_count==1?"":"s",(unsigned)units,units==1?"":"s",bad?", some units ignored":"");return 1;}

char const* cu_debug_dwarf_status(void){return g_status;}uint32_t cu_debug_dwarf_type_count(void){return g_type_count;}uint32_t cu_debug_dwarf_variable_count(void){return g_var_count;}
static int active(variable_t const* v,uint32_t pc){if(v->scope==CU_DBG_DWARF_SCOPE_GLOBAL)return 1;if(v->low_word==0&&v->high_word==0)return 1;if(v->high_word<=v->low_word)return pc>=v->low_word;return pc>=v->low_word&&pc<v->high_word;}
uint32_t cu_debug_dwarf_global_count(void){uint32_t i,n=0;for(i=0;i<g_var_count;i++)if(g_vars[i].scope==CU_DBG_DWARF_SCOPE_GLOBAL)n++;return n;}
uint32_t cu_debug_dwarf_local_count(uint32_t pc){uint32_t i,n=0;for(i=0;i<g_var_count;i++)if(g_vars[i].scope!=CU_DBG_DWARF_SCOPE_GLOBAL&&active(&g_vars[i],pc))n++;return n;}
int cu_debug_dwarf_global_at(uint32_t ord,uint32_t* out){uint32_t i,n=0;for(i=0;i<g_var_count;i++)if(g_vars[i].scope==CU_DBG_DWARF_SCOPE_GLOBAL){if(n++==ord){if(out)*out=i;return 1;}}return 0;}
int cu_debug_dwarf_local_at(uint32_t pc,uint32_t ord,uint32_t* out){uint32_t i,n=0;for(i=0;i<g_var_count;i++)if(g_vars[i].scope!=CU_DBG_DWARF_SCOPE_GLOBAL&&active(&g_vars[i],pc)){if(n++==ord){if(out)*out=i;return 1;}}return 0;}

int cu_debug_dwarf_type_info(uint32_t id,cu_debug_dwarf_type_info_t* out){type_t const* t;if(id>=g_type_count||!out)return 0;t=&g_types[id];out->id=id;out->kind=t->kind;out->byte_size=t->byte_size;out->target_type=t->target_type;out->element_count=t->element_count;out->member_count=t->member_count;out->encoding=t->encoding;out->name=t->name?t->name:"";return 1;}
static int tfmt(uint32_t id,char* out,size_t cap,unsigned depth){type_t const* t;char tmp[256];if(!out||!cap)return 0;if(id==UINT32_MAX||id>=g_type_count){snprintf(out,cap,"?");return 1;}if(depth>12){snprintf(out,cap,"...");return 1;}t=&g_types[id];switch(t->kind){case CU_DBG_DWARF_TYPE_BASE:snprintf(out,cap,"%s",(t->name&&t->name[0])?t->name:"base");break;case CU_DBG_DWARF_TYPE_TYPEDEF:snprintf(out,cap,"%s",(t->name&&t->name[0])?t->name:"typedef");break;case CU_DBG_DWARF_TYPE_POINTER:tfmt(t->target_type,tmp,sizeof(tmp),depth+1);snprintf(out,cap,"%s *",tmp);break;case CU_DBG_DWARF_TYPE_CONST:tfmt(t->target_type,tmp,sizeof(tmp),depth+1);snprintf(out,cap,"const %s",tmp);break;case CU_DBG_DWARF_TYPE_VOLATILE:tfmt(t->target_type,tmp,sizeof(tmp),depth+1);snprintf(out,cap,"volatile %s",tmp);break;case CU_DBG_DWARF_TYPE_ARRAY:tfmt(t->target_type,tmp,sizeof(tmp),depth+1);snprintf(out,cap,"%s[%u]",tmp,(unsigned)t->element_count);break;case CU_DBG_DWARF_TYPE_STRUCT:snprintf(out,cap,"struct %s",(t->name&&t->name[0])?t->name:"<anonymous>");break;case CU_DBG_DWARF_TYPE_UNION:snprintf(out,cap,"union %s",(t->name&&t->name[0])?t->name:"<anonymous>");break;case CU_DBG_DWARF_TYPE_ENUM:snprintf(out,cap,"enum %s",(t->name&&t->name[0])?t->name:"<anonymous>");break;default:snprintf(out,cap,"?");break;}return 1;}
int cu_debug_dwarf_type_format(uint32_t id,char* out,size_t cap){return tfmt(id,out,cap,0);}
int cu_debug_dwarf_member_info(uint32_t tid,uint32_t mi,cu_debug_dwarf_member_info_t* out){uint32_t i,n=0;if(tid>=g_type_count||!out)return 0;for(i=0;i<g_member_count;i++)if(g_members[i].owner_type==tid){if(n++==mi){member_t const* m=&g_members[i];out->index=mi;out->type_id=m->type_id;out->byte_offset=m->byte_offset;out->bit_offset=m->bit_offset;out->bit_size=m->bit_size;out->is_enumerator=m->is_enumerator;out->const_value=m->const_value;out->name=m->name?m->name:"";return 1;}}return 0;}
int cu_debug_dwarf_variable_info(uint32_t id,cu_debug_dwarf_variable_info_t* out){variable_t const* v;function_t const* f=NULL;if(id>=g_var_count||!out)return 0;v=&g_vars[id];if(v->function_id<g_func_count)f=&g_funcs[v->function_id];out->id=id;out->scope=v->scope;out->type_id=v->type_id;out->file_index=v->file_index;out->line=v->line;out->low_word_addr=v->low_word;out->high_word_addr=v->high_word;out->function_id=v->function_id;out->name=v->name?v->name:"";out->function_name=(f&&f->name)?f->name:"";return 1;}

static uint32_t type_size(uint32_t id){uint32_t guard=0;while(id<g_type_count&&guard++<16){type_t const* t=&g_types[id];if(t->byte_size)return t->byte_size;if(t->kind==CU_DBG_DWARF_TYPE_POINTER)return 2U;if(t->kind==CU_DBG_DWARF_TYPE_ARRAY&&t->target_type<g_type_count){uint32_t es=type_size(t->target_type);return es*t->element_count;}id=t->target_type;}return 1U;}
static uint32_t reg_numeric(uint32_t r,uint8_t const regs[32],uint32_t sp,int as_addr){if(r<32U){if(as_addr&&(r==26U||r==28U||r==30U)&&r+1U<32U)return (uint32_t)regs[r]|((uint32_t)regs[r+1U]<<8);return regs[r];}if(r==32U||r==33U)return sp;return 0U;}
static uint32_t norm_data_addr(uint64_t a){uint32_t v=(uint32_t)a;if((v&0xFF0000U)==0x800000U)v&=0xFFFFU;return v&0xFFFFU;}

typedef struct{uint64_t v;uint8_t is_reg;uint8_t reg;} estack_t;
typedef struct{uint32_t kind,address,reg;uint8_t bytes[CU_DBG_DWARF_VALUE_MAX];uint32_t nbytes;uint8_t stack_value;} eval_t;

static int expr_for_pc(expr_t const* e,uint32_t pc_word,uint8_t const** out,uint32_t* olen){uint8_t const* p;uint8_t const* end;uint64_t base=0,maxaddr;if(!e)return 0;if(!e->is_loclist){if(!e->bytes||!e->len)return 0;*out=e->bytes;*olen=e->len;return 1;}if(!g_loc_data||e->loclist_off>=g_loc_size)return 0;p=g_loc_data+e->loclist_off;end=g_loc_data+g_loc_size;maxaddr=(g_addr_size==2)?0xFFFFU:((g_addr_size==4)?0xFFFFFFFFULL:UINT64_MAX);while((size_t)(end-p)>=2U*g_addr_size+2U){uint64_t a=rd_addr(p,g_addr_size),b=rd_addr(p+g_addr_size,g_addr_size);uint16_t l;p+=2U*g_addr_size;if(a==0&&b==0)break;if(a==maxaddr){base=b;continue;}l=rd16(p);p+=2;if((size_t)(end-p)<l)return 0;if(((uint64_t)pc_word*2U)>=base+a&&((uint64_t)pc_word*2U)<base+b){*out=p;*olen=l;return 1;}p+=l;}return 0;}

static int eval_expr(uint8_t const* p,uint32_t len,uint8_t const regs[32],uint32_t sp,cu_debug_dwarf_read_data_fn read_data,void* user,expr_t const* frame, uint32_t pc, eval_t* out,int frame_mode){uint8_t const* end=p+len;estack_t st[STACK_MAX];uint32_t sn=0;eval_t pieces;memset(out,0,sizeof(*out));memset(&pieces,0,sizeof(pieces));while(p<end){uint8_t op=*p++;uint64_t u;int64_t s;if(op>=DW_OP_lit0&&op<DW_OP_lit0+32U){if(sn>=STACK_MAX)return 0;st[sn++]=(estack_t){op-DW_OP_lit0,0,0};continue;}if(op>=DW_OP_reg0&&op<DW_OP_reg0+32U){uint32_t r=op-DW_OP_reg0;if(p<end&&*p==DW_OP_piece){/* handled by piece below */}if(sn>=STACK_MAX)return 0;st[sn++]=(estack_t){reg_numeric(r,regs,sp,frame_mode),1,(uint8_t)r};continue;}if(op>=DW_OP_breg0&&op<DW_OP_breg0+32U){uint32_t r=op-DW_OP_breg0;if(!sleb(&p,end,&s)||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)((int64_t)reg_numeric(r,regs,sp,1)+s),0,0};continue;}switch(op){case DW_OP_addr:if((size_t)(end-p)<g_addr_size||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){norm_data_addr(rd_addr(p,g_addr_size)),0,0};p+=g_addr_size;break;case DW_OP_const1u:if(p>=end||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){*p++,0,0};break;case DW_OP_const1s:if(p>=end||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)(int8_t)*p++,0,0};break;case DW_OP_const2u:if((size_t)(end-p)<2||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){rd16(p),0,0};p+=2;break;case DW_OP_const2s:if((size_t)(end-p)<2||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)(int16_t)rd16(p),0,0};p+=2;break;case DW_OP_const4u:if((size_t)(end-p)<4||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){rd32(p),0,0};p+=4;break;case DW_OP_const4s:if((size_t)(end-p)<4||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)(int32_t)rd32(p),0,0};p+=4;break;case DW_OP_constu:if(!uleb(&p,end,&u)||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){u,0,0};break;case DW_OP_consts:if(!sleb(&p,end,&s)||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)s,0,0};break;case DW_OP_dup:if(!sn||sn>=STACK_MAX)return 0;st[sn]=st[sn-1];sn++;break;case DW_OP_drop:if(!sn)return 0;sn--;break;case DW_OP_over:if(sn<2||sn>=STACK_MAX)return 0;st[sn]=st[sn-2];sn++;break;case DW_OP_swap:if(sn<2)return 0;{estack_t t=st[sn-1];st[sn-1]=st[sn-2];st[sn-2]=t;}break;case DW_OP_plus_uconst:if(!sn||!uleb(&p,end,&u))return 0;st[sn-1].v+=u;st[sn-1].is_reg=0;break;case DW_OP_plus:case DW_OP_minus:case DW_OP_mul:case DW_OP_and:case DW_OP_or:case DW_OP_xor:case DW_OP_shl:case DW_OP_shr:if(sn<2)return 0;{uint64_t b=st[--sn].v,a=st[sn-1].v;switch(op){case DW_OP_plus:a+=b;break;case DW_OP_minus:a-=b;break;case DW_OP_mul:a*=b;break;case DW_OP_and:a&=b;break;case DW_OP_or:a|=b;break;case DW_OP_xor:a^=b;break;case DW_OP_shl:a<<=b;break;default:a>>=b;break;}st[sn-1].v=a;st[sn-1].is_reg=0;}break;case DW_OP_regx:if(!uleb(&p,end,&u)||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){reg_numeric((uint32_t)u,regs,sp,frame_mode),1,(uint8_t)u};break;case DW_OP_bregx:if(!uleb(&p,end,&u)||!sleb(&p,end,&s)||sn>=STACK_MAX)return 0;st[sn++]=(estack_t){(uint64_t)((int64_t)reg_numeric((uint32_t)u,regs,sp,1)+s),0,0};break;case DW_OP_fbreg:{eval_t fb;uint8_t const* fe;uint32_t fl;if(!sleb(&p,end,&s)||!frame||!expr_for_pc(frame,pc,&fe,&fl)||!eval_expr(fe,fl,regs,sp,read_data,user,NULL,pc,&fb,1)||sn>=STACK_MAX)return 0;{uint32_t base=(fb.kind==CU_DBG_DWARF_LOC_REGISTER)?reg_numeric(fb.reg,regs,sp,1):fb.address;st[sn++]=(estack_t){(uint64_t)((int64_t)base+s),0,0};}break;}case DW_OP_piece:if(!uleb(&p,end,&u)||!sn)return 0;{estack_t x=st[--sn];uint32_t k;for(k=0;k<(uint32_t)u&&pieces.nbytes<CU_DBG_DWARF_VALUE_MAX;k++){uint8_t v;if(x.is_reg&&x.reg+k<32)v=regs[x.reg+k];else if(read_data)v=read_data(user,norm_data_addr(x.v+k));else v=0;pieces.bytes[pieces.nbytes++]=v;}pieces.kind=CU_DBG_DWARF_LOC_VALUE;}break;case DW_OP_deref:case DW_OP_deref_size:if(!sn||!read_data)return 0;{uint32_t n=(op==DW_OP_deref_size)?(p<end?*p++:0U):2U;uint64_t v=0;uint32_t k;if(!n||n>4)return 0;for(k=0;k<n;k++)v|=((uint64_t)read_data(user,norm_data_addr(st[sn-1].v+k)))<<(8U*k);st[sn-1].v=v;st[sn-1].is_reg=0;}break;case DW_OP_stack_value:out->stack_value=1;break;case DW_OP_nop:break;default:return 0;}}
 if(pieces.nbytes){*out=pieces;return 1;}if(!sn)return 0;if(st[sn-1].is_reg&&!out->stack_value){out->kind=CU_DBG_DWARF_LOC_REGISTER;out->reg=st[sn-1].reg;out->address=(uint32_t)st[sn-1].v;}else if(out->stack_value){out->kind=CU_DBG_DWARF_LOC_VALUE;out->address=(uint32_t)st[sn-1].v;}else{out->kind=CU_DBG_DWARF_LOC_ADDRESS;out->address=norm_data_addr(st[sn-1].v);}return 1;}

static function_t const* var_func(variable_t const* v){return (v&&v->function_id<g_func_count)?&g_funcs[v->function_id]:NULL;}
static void fill_value(uint32_t tid,eval_t const* ev,uint8_t const regs[32],cu_debug_dwarf_read_data_fn rd,void* user,cu_debug_dwarf_value_t* out){uint32_t n=type_size(tid),i;memset(out,0,sizeof(*out));out->status=1;out->location_kind=ev->kind;out->address=ev->address;out->reg=ev->reg;out->byte_size=n;if(n>CU_DBG_DWARF_VALUE_MAX)n=CU_DBG_DWARF_VALUE_MAX;if(ev->kind==CU_DBG_DWARF_LOC_VALUE&&ev->nbytes){out->value_size=ev->nbytes;memcpy(out->value,ev->bytes,ev->nbytes);}else if(ev->kind==CU_DBG_DWARF_LOC_REGISTER){out->value_size=n;for(i=0;i<n;i++)out->value[i]=(ev->reg+i<32U)?regs[ev->reg+i]:0;}else if(ev->kind==CU_DBG_DWARF_LOC_ADDRESS&&rd){out->value_size=n;for(i=0;i<n;i++)out->value[i]=rd(user,ev->address+i);}else if(ev->kind==CU_DBG_DWARF_LOC_VALUE){out->value_size=n;for(i=0;i<n&&i<8U;i++)out->value[i]=(uint8_t)(ev->address>>(8U*i));}}
int cu_debug_dwarf_eval_variable(uint32_t id,uint32_t pc,uint8_t const regs[32],uint32_t sp,cu_debug_dwarf_read_data_fn rd,void* user,cu_debug_dwarf_value_t* out){variable_t const* v;function_t const* f;uint8_t const* ex;uint32_t el;eval_t ev;if(!out||id>=g_var_count||!regs)return 0;memset(out,0,sizeof(*out));v=&g_vars[id];if(!active(v,pc)){out->location_kind=CU_DBG_DWARF_LOC_OPTIMIZED;snprintf(out->message,sizeof(out->message),"out of scope");return 1;}if(!expr_for_pc(&v->loc,pc,&ex,&el)){out->location_kind=CU_DBG_DWARF_LOC_OPTIMIZED;snprintf(out->message,sizeof(out->message),"optimized out / no location for this PC");return 1;}f=var_func(v);if(!eval_expr(ex,el,regs,sp,rd,user,f?&f->frame_base:NULL,pc,&ev,0)){out->location_kind=CU_DBG_DWARF_LOC_UNSUPPORTED;snprintf(out->message,sizeof(out->message),"unsupported DWARF location expression");return 1;}fill_value(v->type_id,&ev,regs,rd,user,out);return 1;}
int cu_debug_dwarf_eval_member(uint32_t vid,uint32_t mi,uint32_t pc,uint8_t const regs[32],uint32_t sp,cu_debug_dwarf_read_data_fn rd,void* user,cu_debug_dwarf_value_t* out,uint32_t* out_tid){cu_debug_dwarf_value_t base;variable_t const* v;uint32_t tid,real=UINT32_MAX,i,n=0;eval_t ev;if(vid>=g_var_count||!out)return 0;v=&g_vars[vid];tid=v->type_id;while(tid<g_type_count&&(g_types[tid].kind==CU_DBG_DWARF_TYPE_TYPEDEF||g_types[tid].kind==CU_DBG_DWARF_TYPE_CONST||g_types[tid].kind==CU_DBG_DWARF_TYPE_VOLATILE))tid=g_types[tid].target_type;if(tid>=g_type_count)return 0;for(i=0;i<g_member_count;i++)if(g_members[i].owner_type==tid){if(n++==mi){real=i;break;}}if(real==UINT32_MAX)return 0;if(!cu_debug_dwarf_eval_variable(vid,pc,regs,sp,rd,user,&base))return 0;if(base.location_kind!=CU_DBG_DWARF_LOC_ADDRESS){memset(out,0,sizeof(*out));out->location_kind=CU_DBG_DWARF_LOC_UNSUPPORTED;snprintf(out->message,sizeof(out->message),"aggregate is not addressable");return 1;}memset(&ev,0,sizeof(ev));ev.kind=CU_DBG_DWARF_LOC_ADDRESS;ev.address=base.address+g_members[real].byte_offset;fill_value(g_members[real].type_id,&ev,regs,rd,user,out);if(g_members[real].bit_size&&g_members[real].bit_offset>=0&&out->value_size&&out->value_size<=8U){uint64_t raw=0U,mask;uint32_t k,bits=out->value_size*8U,shift;for(k=0;k<out->value_size;k++)raw|=((uint64_t)out->value[k])<<(8U*k);if((uint32_t)g_members[real].bit_offset+g_members[real].bit_size<=bits){shift=bits-(uint32_t)g_members[real].bit_offset-g_members[real].bit_size;mask=(g_members[real].bit_size>=64U)?~0ULL:((1ULL<<g_members[real].bit_size)-1ULL);raw=(raw>>shift)&mask;out->value_size=(g_members[real].bit_size+7U)/8U;out->byte_size=out->value_size;for(k=0;k<out->value_size;k++)out->value[k]=(uint8_t)(raw>>(8U*k));}}if(out_tid)*out_tid=g_members[real].type_id;return 1;}
int cu_debug_dwarf_eval_element(uint32_t vid,uint32_t ei,uint32_t pc,uint8_t const regs[32],uint32_t sp,cu_debug_dwarf_read_data_fn rd,void* user,cu_debug_dwarf_value_t* out,uint32_t* out_tid){cu_debug_dwarf_value_t base;variable_t const* v;uint32_t tid,et,esz;eval_t ev;if(vid>=g_var_count||!out)return 0;v=&g_vars[vid];tid=v->type_id;while(tid<g_type_count&&(g_types[tid].kind==CU_DBG_DWARF_TYPE_TYPEDEF||g_types[tid].kind==CU_DBG_DWARF_TYPE_CONST||g_types[tid].kind==CU_DBG_DWARF_TYPE_VOLATILE))tid=g_types[tid].target_type;if(tid>=g_type_count||g_types[tid].kind!=CU_DBG_DWARF_TYPE_ARRAY||ei>=g_types[tid].element_count)return 0;et=g_types[tid].target_type;esz=type_size(et);if(!cu_debug_dwarf_eval_variable(vid,pc,regs,sp,rd,user,&base))return 0;if(base.location_kind!=CU_DBG_DWARF_LOC_ADDRESS){memset(out,0,sizeof(*out));out->location_kind=CU_DBG_DWARF_LOC_UNSUPPORTED;snprintf(out->message,sizeof(out->message),"array is not addressable");return 1;}memset(&ev,0,sizeof(ev));ev.kind=CU_DBG_DWARF_LOC_ADDRESS;ev.address=base.address+ei*esz;fill_value(et,&ev,regs,rd,user,out);if(out_tid)*out_tid=et;return 1;}
