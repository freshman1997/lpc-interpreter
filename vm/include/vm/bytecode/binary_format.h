#ifndef LPC_VM_BYTECODE_BINARY_FORMAT_H
#define LPC_VM_BYTECODE_BINARY_FORMAT_H

#include <cstdint>
#include <string>

namespace lpc {
namespace vm {

// ============================================================================
// LPC Bytecode Binary Format Specification (V2)
// ============================================================================
//
// Preamble:
//   magic:    4 bytes  "LPC\0"       (0x4C 0x50 0x43 0x00)
//   version:  4 bytes  LE u32       currently 2
//
// Sections (in order; all optional except HEADER):
//   Each section: id(1 byte) + size(u32 LE) + payload(size bytes)
//
//   Unknown section IDs are skipped using the size field (forward compat).
//
// Section IDs:
//   0x00  HEADER        (required) module name, flags
//   0x01  CONSTANTS     int/float/string constant pools
//   0x02  CLASSES       class definitions + field names
//   0x03  FUNCTIONS     function prototypes + upvalues
//   0x04  LINE_TABLE    PC -> source line mapping
//   0x05  CODE          bytecode + init_code
//   0x06  DEBUG         debug symbols (variable names, source file, etc.)
//   0x07  LIFECYCLE     create/on_loadin/on_destruct function indices
//   0x07..0xFF          reserved for future use (skip via size)
//
// SECTION 0x00 - HEADER:
//   module_name:  u32(len) + len bytes
//   flags:        u32  (bit0 = has_init_code, bit1 = has_debug_info)
//   source_file:  u32(len) + len bytes  (original .lpc source path, V2)
//
// SECTION 0x01 - CONSTANTS:
//   n_iconst:     u32
//   iconst[n]:    i64 (8 bytes LE) each
//   n_fconst:     u32
//   fconst[n]:    f64 (8 bytes LE, IEEE754) each
//   n_sconst:     u32
//   sconst[n]:    u32(len) + len bytes each
//
// SECTION 0x02 - CLASSES:
//   n_classes:    u32
//   class[n]:
//     name:            u32(len) + len bytes
//     nfields:         u16
//     parent_class_idx: u16  (0xFFFF = no parent)
//     field_name[nfields]: u32(len) + len bytes each  (V2, for debug)
//
// SECTION 0x03 - FUNCTIONS:
//   n_functions:  u32
//   func[n]:
//     name:           u32(len) + len bytes
//     arity:          u16
//     nlocals:        u16
//     max_stack:      u16
//     code_start:     u32
//     code_end:       u32
//     n_upvalues:     u16
//     upvalue[n]:
//       source_kind:  u16
//       source_index: u16
//
// SECTION 0x04 - LINE_TABLE:
//   n_entries:    u32
//   entry[n]:
//     line:        u32
//     pc:          u32
//
// SECTION 0x05 - CODE:
//   code_size:    u32
//   code:         code_size bytes
//   init_size:    u32
//   init_code:    init_size bytes  (only if HEADER flags bit0 is set)
//
// SECTION 0x06 - DEBUG:
//   source_file:  u32(len) + len bytes  (original .lpc source path)
//   n_globals:    u32
//   global_name[n]: u32(len) + len bytes each
//   n_func_debug: u32
//   func_debug[n]:
//     n_param_names:  u16
//     param_name[n]:  u32(len) + len bytes each
//     n_local_names:  u16
//     local_name[n]:  u32(len) + len bytes each
//     n_upvalue_names: u16
//     upvalue_name[n]: u32(len) + len bytes each
//
// SECTION 0x07 - LIFECYCLE:
//   create_idx:      u16  (0xFFFF = not present)
//   on_loadin_idx:   u16  (0xFFFF = not present)
//   on_destruct_idx: u16  (0xFFFF = not present)
//
// V1 Compatibility:
//   V1 files start with "LPCNVM1\0" (8 bytes).
//   V2 files start with "LPC\0" + version(u32=2) (8 bytes).
//   Reader auto-detects based on magic bytes.
//
// Checksum:
//   After all sections, a CRC32 over the entire file (excluding the CRC itself)
//   can optionally appear as a final u32. Readers should check this if present.
//   Presence is indicated by HEADER flags bit2.
//
// ============================================================================

constexpr std::uint32_t kMagicV2   = 0x0043504C; // "LPC\0" in LE
constexpr std::uint8_t  kMagicV1_0 = 0x4C; // 'L' -- V1 starts with "LPCNVM1\0"
constexpr std::uint8_t  kMagicV1_1 = 0x50; // 'P'
constexpr std::uint8_t  kMagicV1_2 = 0x43; // 'C'
constexpr std::uint8_t  kMagicV1_3 = 0x4E; // 'N'
constexpr std::uint32_t kVersion2  = 2;

constexpr std::uint8_t kSecHeader     = 0x00;
constexpr std::uint8_t kSecConstants  = 0x01;
constexpr std::uint8_t kSecClasses    = 0x02;
constexpr std::uint8_t kSecFunctions  = 0x03;
constexpr std::uint8_t kSecLineTable  = 0x04;
constexpr std::uint8_t kSecCode       = 0x05;
constexpr std::uint8_t kSecDebug      = 0x06;
constexpr std::uint8_t kSecLifecycle  = 0x07;

constexpr std::uint32_t kFlagHasInitCode  = 0x01;
constexpr std::uint32_t kFlagHasDebugInfo = 0x02;
constexpr std::uint32_t kFlagHasCrc32     = 0x04;

struct Chunk;

std::string SerializeChunk(const Chunk &chunk);
bool DeserializeChunk(const std::string &data, Chunk *out_chunk);

} // namespace vm
} // namespace lpc

#endif
