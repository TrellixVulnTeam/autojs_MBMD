// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

#ifndef V8_ASSEMBLER_H_
#define V8_ASSEMBLER_H_

#include "src/allocation.h"
#include "src/builtins.h"
#include "src/isolate.h"
#include "src/runtime/runtime.h"
#include "src/token.h"

namespace v8 {

// Forward declarations.
class ApiFunction;

namespace internal {

// Forward declarations.
class SourcePosition;
class StatsCounter;

// -----------------------------------------------------------------------------
// Platform independent assembler base class.

class AssemblerBase: public Malloced {
 public:
  AssemblerBase(Isolate* isolate, void* buffer, int buffer_size);
  virtual ~AssemblerBase();

  Isolate* isolate() const { return isolate_; }
  int jit_cookie() const { return jit_cookie_; }

  bool emit_debug_code() const { return emit_debug_code_; }
  void set_emit_debug_code(bool value) { emit_debug_code_ = value; }

  bool serializer_enabled() const { return serializer_enabled_; }
  void enable_serializer() { serializer_enabled_ = true; }

  bool predictable_code_size() const { return predictable_code_size_; }
  void set_predictable_code_size(bool value) { predictable_code_size_ = value; }

  uint64_t enabled_cpu_features() const { return enabled_cpu_features_; }
  void set_enabled_cpu_features(uint64_t features) {
    enabled_cpu_features_ = features;
  }
  bool IsEnabled(CpuFeature f) {
    return (enabled_cpu_features_ & (static_cast<uint64_t>(1) << f)) != 0;
  }

  bool is_constant_pool_available() const {
    if (FLAG_enable_embedded_constant_pool) {
      return constant_pool_available_;
    } else {
      // Embedded constant pool not supported on this architecture.
      UNREACHABLE();
      return false;
    }
  }

  // Overwrite a host NaN with a quiet target NaN.  Used by mksnapshot for
  // cross-snapshotting.
  static void QuietNaN(HeapObject* nan) { }

  int pc_offset() const { return static_cast<int>(pc_ - buffer_); }

  // This function is called when code generation is aborted, so that
  // the assembler could clean up internal data structures.
  virtual void AbortedCodeGeneration() { }

  static const int kMinimalBufferSize = 4*KB;

  static void FlushICache(Isolate* isolate, void* start, size_t size);

  // TODO(all): Help get rid of this one.
  static void FlushICacheWithoutIsolate(void* start, size_t size);

 protected:
  // The buffer into which code and relocation info are generated. It could
  // either be owned by the assembler or be provided externally.
  byte* buffer_;
  int buffer_size_;
  bool own_buffer_;

  void set_constant_pool_available(bool available) {
    if (FLAG_enable_embedded_constant_pool) {
      constant_pool_available_ = available;
    } else {
      // Embedded constant pool not supported on this architecture.
      UNREACHABLE();
    }
  }

  // The program counter, which points into the buffer above and moves forward.
  byte* pc_;

 private:
  Isolate* isolate_;
  int jit_cookie_;
  uint64_t enabled_cpu_features_;
  bool emit_debug_code_;
  bool predictable_code_size_;
  bool serializer_enabled_;

  // Indicates whether the constant pool can be accessed, which is only possible
  // if the pp register points to the current code object's constant pool.
  bool constant_pool_available_;

  // Constant pool.
  friend class FrameAndConstantPoolScope;
  friend class ConstantPoolUnavailableScope;
};


// Avoids emitting debug code during the lifetime of this scope object.
class DontEmitDebugCodeScope BASE_EMBEDDED {
 public:
  explicit DontEmitDebugCodeScope(AssemblerBase* assembler)
      : assembler_(assembler), old_value_(assembler->emit_debug_code()) {
    assembler_->set_emit_debug_code(false);
  }
  ~DontEmitDebugCodeScope() {
    assembler_->set_emit_debug_code(old_value_);
  }
 private:
  AssemblerBase* assembler_;
  bool old_value_;
};


// Avoids using instructions that vary in size in unpredictable ways between the
// snapshot and the running VM.
class PredictableCodeSizeScope {
 public:
  explicit PredictableCodeSizeScope(AssemblerBase* assembler);
  PredictableCodeSizeScope(AssemblerBase* assembler, int expected_size);
  ~PredictableCodeSizeScope();
  void ExpectSize(int expected_size) { expected_size_ = expected_size; }

 private:
  AssemblerBase* assembler_;
  int expected_size_;
  int start_offset_;
  bool old_value_;
};


// Enable a specified feature within a scope.
class CpuFeatureScope BASE_EMBEDDED {
 public:
#ifdef DEBUG
  CpuFeatureScope(AssemblerBase* assembler, CpuFeature f);
  ~CpuFeatureScope();

 private:
  AssemblerBase* assembler_;
  uint64_t old_enabled_;
#else
  CpuFeatureScope(AssemblerBase* assembler, CpuFeature f) {}
#endif
};


// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a CpuFeatureScope before use.
// Example:
//   if (assembler->IsSupported(SSE3)) {
//     CpuFeatureScope fscope(assembler, SSE3);
//     // Generate code containing SSE3 instructions.
//   } else {
//     // Generate alternative code.
//   }
class CpuFeatures : public AllStatic {
 public:
  static void Probe(bool cross_compile) {
    STATIC_ASSERT(NUMBER_OF_CPU_FEATURES <= kBitsPerInt);
    if (initialized_) return;
    initialized_ = true;
    ProbeImpl(cross_compile);
  }

  static unsigned SupportedFeatures() {
    Probe(false);
    return supported_;
  }

  static bool IsSupported(CpuFeature f) {
    return (supported_ & (1u << f)) != 0;
  }

  static inline bool SupportsCrankshaft();

  static inline unsigned cache_line_size() {
    DCHECK(cache_line_size_ != 0);
    return cache_line_size_;
  }

  static void PrintTarget();
  static void PrintFeatures();

  // Flush instruction cache.
  static void FlushICache(void* start, size_t size);

 private:
  // Platform-dependent implementation.
  static void ProbeImpl(bool cross_compile);

  static unsigned supported_;
  static unsigned cache_line_size_;
  static bool initialized_;
  friend class ExternalReference;
  DISALLOW_COPY_AND_ASSIGN(CpuFeatures);
};


// -----------------------------------------------------------------------------
// Labels represent pc locations; they are typically jump or call targets.
// After declaration, a label can be freely used to denote known or (yet)
// unknown pc location. Assembler::bind() is used to bind a label to the
// current pc. A label can be bound only once.

class Label {
 public:
  enum Distance {
    kNear, kFar
  };

  INLINE(Label()) {
    Unuse();
    UnuseNear();
  }

  INLINE(~Label()) {
    DCHECK(!is_linked());
    DCHECK(!is_near_linked());
  }

  INLINE(void Unuse()) { pos_ = 0; }
  INLINE(void UnuseNear()) { near_link_pos_ = 0; }

  INLINE(bool is_bound() const) { return pos_ <  0; }
  INLINE(bool is_unused() const) { return pos_ == 0 && near_link_pos_ == 0; }
  INLINE(bool is_linked() const) { return pos_ >  0; }
  INLINE(bool is_near_linked() const) { return near_link_pos_ > 0; }

  // Returns the position of bound or linked labels. Cannot be used
  // for unused labels.
  int pos() const;
  int near_link_pos() const { return near_link_pos_ - 1; }

 private:
  // pos_ encodes both the binding state (via its sign)
  // and the binding position (via its value) of a label.
  //
  // pos_ <  0  bound label, pos() returns the jump target position
  // pos_ == 0  unused label
  // pos_ >  0  linked label, pos() returns the last reference position
  int pos_;

  // Behaves like |pos_| in the "> 0" case, but for near jumps to this label.
  int near_link_pos_;

  void bind_to(int pos)  {
    pos_ = -pos - 1;
    DCHECK(is_bound());
  }
  void link_to(int pos, Distance distance = kFar) {
    if (distance == kNear) {
      near_link_pos_ = pos + 1;
      DCHECK(is_near_linked());
    } else {
      pos_ = pos + 1;
      DCHECK(is_linked());
    }
  }

  friend class Assembler;
  friend class Displacement;
  friend class RegExpMacroAssemblerIrregexp;

#if V8_TARGET_ARCH_ARM64
  // On ARM64, the Assembler keeps track of pointers to Labels to resolve
  // branches to distant targets. Copying labels would confuse the Assembler.
  DISALLOW_COPY_AND_ASSIGN(Label);  // NOLINT
#endif
};


enum SaveFPRegsMode { kDontSaveFPRegs, kSaveFPRegs };

enum ArgvMode { kArgvOnStack, kArgvInRegister };

// Specifies whether to perform icache flush operations on RelocInfo updates.
// If FLUSH_ICACHE_IF_NEEDED, the icache will always be flushed if an
// instruction was modified. If SKIP_ICACHE_FLUSH the flush will always be
// skipped (only use this if you will flush the icache manually before it is
// executed).
enum ICacheFlushMode { FLUSH_ICACHE_IF_NEEDED, SKIP_ICACHE_FLUSH };

// -----------------------------------------------------------------------------
// Relocation information


// Relocation information consists of the address (pc) of the datum
// to which the relocation information applies, the relocation mode
// (rmode), and an optional data field. The relocation mode may be
// "descriptive" and not indicate a need for relocation, but simply
// describe a property of the datum. Such rmodes are useful for GC
// and nice disassembly output.

class RelocInfo {
 public:
  // The constant kNoPosition is used with the collecting of source positions
  // in the relocation information. Two types of source positions are collected
  // "position" (RelocMode position) and "statement position" (RelocMode
  // statement_position). The "position" is collected at places in the source
  // code which are of interest when making stack traces to pin-point the source
  // location of a stack frame as close as possible. The "statement position" is
  // collected at the beginning at each statement, and is used to indicate
  // possible break locations. kNoPosition is used to indicate an
  // invalid/uninitialized position value.
  static const int kNoPosition = -1;

  // This string is used to add padding comments to the reloc info in cases
  // where we are not sure to have enough space for patching in during
  // lazy deoptimization. This is the case if we have indirect calls for which
  // we do not normally record relocation info.
  static const char* const kFillerCommentString;

  // The minimum size of a comment is equal to two bytes for the extra tagged
  // pc and kPointerSize for the actual pointer to the comment.
  static const int kMinRelocCommentSize = 2 + kPointerSize;

  // The maximum size for a call instruction including pc-jump.
  static const int kMaxCallSize = 6;

  // The maximum pc delta that will use the short encoding.
  static const int kMaxSmallPCDelta;

  enum Mode {
    // Please note the order is important (see IsCodeTarget, IsGCRelocMode).
    CODE_TARGET,  // Code target which is not any of the above.
    CODE_TARGET_WITH_ID,
    CONSTRUCT_CALL,  // code target that is a call to a JavaScript constructor.
    DEBUGGER_STATEMENT,  // Code target for the debugger statement.
    EMBEDDED_OBJECT,
    CELL,

    // Everything after runtime_entry (inclusive) is not GC'ed.
    RUNTIME_ENTRY,
    COMMENT,
    POSITION,            // See comment for kNoPosition above.
    STATEMENT_POSITION,  // See comment for kNoPosition above.

    // Additional code inserted for debug break slot.
    DEBUG_BREAK_SLOT_AT_POSITION,
    DEBUG_BREAK_SLOT_AT_RETURN,
    DEBUG_BREAK_SLOT_AT_CALL,
    DEBUG_BREAK_SLOT_AT_CONSTRUCT_CALL,

    EXTERNAL_REFERENCE,  // The address of an external C++ function.
    INTERNAL_REFERENCE,  // An address inside the same function.

    // Encoded internal reference, used only on MIPS, MIPS64 and PPC.
    INTERNAL_REFERENCE_ENCODED,

    // Continuation points for a generator yield.
    GENERATOR_CONTINUATION,

    // Marks constant and veneer pools. Only used on ARM and ARM64.
    // They use a custom noncompact encoding.
    CONST_POOL,
    VENEER_POOL,

    DEOPT_REASON,  // Deoptimization reason index.

    // This is not an actual reloc mode, but used to encode a long pc jump that
    // cannot be encoded as part of another record.
    PC_JUMP,

    // Pseudo-types
    NUMBER_OF_MODES,
    NONE32,             // never recorded 32-bit value
    NONE64,             // never recorded 64-bit value
    CODE_AGE_SEQUENCE,  // Not stored in RelocInfo array, used explictly by
                        // code aging.

    FIRST_REAL_RELOC_MODE = CODE_TARGET,
    LAST_REAL_RELOC_MODE = VENEER_POOL,
    LAST_CODE_ENUM = DEBUGGER_STATEMENT,
    LAST_GCED_ENUM = CELL,
  };

  STATIC_ASSERT(NUMBER_OF_MODES <= kBitsPerInt);

  RelocInfo() {}

  RelocInfo(byte* pc, Mode rmode, intptr_t data, Code* host)
      : pc_(pc), rmode_(rmode), data_(data), host_(host) {
  }

  static inline bool IsRealRelocMode(Mode mode) {
    return mode >= FIRST_REAL_RELOC_MODE &&
        mode <= LAST_REAL_RELOC_MODE;
  }
  static inline bool IsConstructCall(Mode mode) {
    return mode == CONSTRUCT_CALL;
  }
  static inline bool IsCodeTarget(Mode mode) {
    return mode <= LAST_CODE_ENUM;
  }
  static inline bool IsEmbeddedObject(Mode mode) {
    return mode == EMBEDDED_OBJECT;
  }
  static inline bool IsCell(Mode mode) { return mode == CELL; }
  static inline bool IsRuntimeEntry(Mode mode) {
    return mode == RUNTIME_ENTRY;
  }
  // Is the relocation mode affected by GC?
  static inline bool IsGCRelocMode(Mode mode) {
    return mode <= LAST_GCED_ENUM;
  }
  static inline bool IsComment(Mode mode) {
    return mode == COMMENT;
  }
  static inline bool IsConstPool(Mode mode) {
    return mode == CONST_POOL;
  }
  static inline bool IsVeneerPool(Mode mode) {
    return mode == VENEER_POOL;
  }
  static inline bool IsDeoptReason(Mode mode) {
    return mode == DEOPT_REASON;
  }
  static inline bool IsPosition(Mode mode) {
    return mode == POSITION || mode == STATEMENT_POSITION;
  }
  static inline bool IsStatementPosition(Mode mode) {
    return mode == STATEMENT_POSITION;
  }
  static inline bool IsExternalReference(Mode mode) {
    return mode == EXTERNAL_REFERENCE;
  }
  static inline bool IsInternalReference(Mode mode) {
    return mode == INTERNAL_REFERENCE;
  }
  static inline bool IsInternalReferenceEncoded(Mode mode) {
    return mode == INTERNAL_REFERENCE_ENCODED;
  }
  static inline bool IsDebugBreakSlot(Mode mode) {
    return IsDebugBreakSlotAtPosition(mode) || IsDebugBreakSlotAtReturn(mode) ||
           IsDebugBreakSlotAtCall(mode) ||
           IsDebugBreakSlotAtConstructCall(mode);
  }
  static inline bool IsDebugBreakSlotAtPosition(Mode mode) {
    return mode == DEBUG_BREAK_SLOT_AT_POSITION;
  }
  static inline bool IsDebugBreakSlotAtReturn(Mode mode) {
    return mode == DEBUG_BREAK_SLOT_AT_RETURN;
  }
  static inline bool IsDebugBreakSlotAtCall(Mode mode) {
    return mode == DEBUG_BREAK_SLOT_AT_CALL;
  }
  static inline bool IsDebugBreakSlotAtConstructCall(Mode mode) {
    return mode == DEBUG_BREAK_SLOT_AT_CONSTRUCT_CALL;
  }
  static inline bool IsDebuggerStatement(Mode mode) {
    return mode == DEBUGGER_STATEMENT;
  }
  static inline bool IsNone(Mode mode) {
    return mode == NONE32 || mode == NONE64;
  }
  static inline bool IsCodeAgeSequence(Mode mode) {
    return mode == CODE_AGE_SEQUENCE;
  }
  static inline bool IsGeneratorContinuation(Mode mode) {
    return mode == GENERATOR_CONTINUATION;
  }
  static inline int ModeMask(Mode mode) { return 1 << mode; }

  // Accessors
  byte* pc() const { return pc_; }
  void set_pc(byte* pc) { pc_ = pc; }
  Mode rmode() const {  return rmode_; }
  intptr_t data() const { return data_; }
  Code* host() const { return host_; }
  void set_host(Code* host) { host_ = host; }

  // Apply a relocation by delta bytes. When the code object is moved, PC
  // relative addresses have to be updated as well as absolute addresses
  // inside the code (internal references).
  // Do not forget to flush the icache afterwards!
  INLINE(void apply(intptr_t delta));

  // Is the pointer this relocation info refers to coded like a plain pointer
  // or is it strange in some way (e.g. relative or patched into a series of
  // instructions).
  bool IsCodedSpecially();

  // If true, the pointer this relocation info refers to is an entry in the
  // constant pool, otherwise the pointer is embedded in the instruction stream.
  bool IsInConstantPool();

  static int DebugBreakCallArgumentsCount(intptr_t data);

  // Read/modify the code target in the branch/call instruction
  // this relocation applies to;
  // can only be called if IsCodeTarget(rmode_) || IsRuntimeEntry(rmode_)
  INLINE(Address target_address());
  INLINE(void set_target_address(Address target,
                                 WriteBarrierMode write_barrier_mode =
                                     UPDATE_WRITE_BARRIER,
                                 ICacheFlushMode icache_flush_mode =
                                     FLUSH_ICACHE_IF_NEEDED));
  INLINE(Object* target_object());
  INLINE(Handle<Object> target_object_handle(Assembler* origin));
  INLINE(void set_target_object(Object* target,
                                WriteBarrierMode write_barrier_mode =
                                    UPDATE_WRITE_BARRIER,
                                ICacheFlushMode icache_flush_mode =
                                    FLUSH_ICACHE_IF_NEEDED));
  INLINE(Address target_runtime_entry(Assembler* origin));
  INLINE(void set_target_runtime_entry(Address target,
                                       WriteBarrierMode write_barrier_mode =
                                           UPDATE_WRITE_BARRIER,
                                       ICacheFlushMode icache_flush_mode =
                                           FLUSH_ICACHE_IF_NEEDED));
  INLINE(Cell* target_cell());
  INLINE(Handle<Cell> target_cell_handle());
  INLINE(void set_target_cell(Cell* cell,
                              WriteBarrierMode write_barrier_mode =
                                  UPDATE_WRITE_BARRIER,
                              ICacheFlushMode icache_flush_mode =
                                  FLUSH_ICACHE_IF_NEEDED));
  INLINE(Handle<Object> code_age_stub_handle(Assembler* origin));
  INLINE(Code* code_age_stub());
  INLINE(void set_code_age_stub(Code* stub,
                                ICacheFlushMode icache_flush_mode =
                                    FLUSH_ICACHE_IF_NEEDED));

  // Returns the address of the constant pool entry where the target address
  // is held.  This should only be called if IsInConstantPool returns true.
  INLINE(Address constant_pool_entry_address());

  // Read the address of the word containing the target_address in an
  // instruction stream.  What this means exactly is architecture-independent.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.  Architecture-independent code shouldn't
  // dereference the pointer it gets back from this.
  INLINE(Address target_address_address());

  // This indicates how much space a target takes up when deserializing a code
  // stream.  For most architectures this is just the size of a pointer.  For
  // an instruction like movw/movt where the target bits are mixed into the
  // instruction bits the size of the target will be zero, indicating that the
  // serializer should not step forwards in memory after a target is resolved
  // and written.  In this case the target_address_address function above
  // should return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target.
  INLINE(int target_address_size());

  // Read the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is EXTERNAL_REFERENCE.
  INLINE(Address target_external_reference());

  // Read the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is INTERNAL_REFERENCE.
  INLINE(Address target_internal_reference());

  // Return the reference address this relocation applies to;
  // can only be called if rmode_ is INTERNAL_REFERENCE.
  INLINE(Address target_internal_reference_address());

  // Read/modify the address of a call instruction. This is used to relocate
  // the break points where straight-line code is patched with a call
  // instruction.
  INLINE(Address debug_call_address());
  INLINE(void set_debug_call_address(Address target));

  // Wipe out a relocation to a fixed value, used for making snapshots
  // reproducible.
  INLINE(void WipeOut());

  template<typename StaticVisitor> inline void Visit(Heap* heap);
  inline void Visit(Isolate* isolate, ObjectVisitor* v);

  // Patch the code with a call.
  void PatchCodeWithCall(Address target, int guard_bytes);

  // Check whether this return sequence has been patched
  // with a call to the debugger.
  INLINE(bool IsPatchedReturnSequence());

  // Check whether this debug break slot has been patched with a call to the
  // debugger.
  INLINE(bool IsPatchedDebugBreakSlotSequence());

#ifdef DEBUG
  // Check whether the given code contains relocation information that
  // either is position-relative or movable by the garbage collector.
  static bool RequiresRelocation(const CodeDesc& desc);
#endif

#ifdef ENABLE_DISASSEMBLER
  // Printing
  static const char* RelocModeName(Mode rmode);
  void Print(Isolate* isolate, std::ostream& os);  // NOLINT
#endif  // ENABLE_DISASSEMBLER
#ifdef VERIFY_HEAP
  void Verify(Isolate* isolate);
#endif

  static const int kCodeTargetMask = (1 << (LAST_CODE_ENUM + 1)) - 1;
  static const int kPositionMask = 1 << POSITION | 1 << STATEMENT_POSITION;
  static const int kDataMask =
      (1 << CODE_TARGET_WITH_ID) | kPositionMask | (1 << COMMENT);
  static const int kDebugBreakSlotMask =
      1 << DEBUG_BREAK_SLOT_AT_POSITION | 1 << DEBUG_BREAK_SLOT_AT_RETURN |
      1 << DEBUG_BREAK_SLOT_AT_CALL | 1 << DEBUG_BREAK_SLOT_AT_CONSTRUCT_CALL;
  static const int kApplyMask;  // Modes affected by apply.  Depends on arch.

 private:
  // On ARM, note that pc_ is the address of the constant pool entry
  // to be relocated and not the address of the instruction
  // referencing the constant pool entry (except when rmode_ ==
  // comment).
  byte* pc_;
  Mode rmode_;
  intptr_t data_;
  Code* host_;
  // External-reference pointers are also split across instruction-pairs
  // on some platforms, but are accessed via indirect pointers. This location
  // provides a place for that pointer to exist naturally. Its address
  // is returned by RelocInfo::target_reference_address().
  Address reconstructed_adr_ptr_;
  friend class RelocIterator;
};


// RelocInfoWriter serializes a stream of relocation info. It writes towards
// lower addresses.
class RelocInfoWriter BASE_EMBEDDED {
 public:
  RelocInfoWriter()
      : pos_(NULL),
        last_pc_(NULL),
        last_id_(0),
        last_position_(0),
        last_mode_(RelocInfo::NUMBER_OF_MODES),
        next_position_candidate_pos_delta_(0),
        next_position_candidate_pc_delta_(0),
        next_position_candidate_flushed_(true) {}
  RelocInfoWriter(byte* pos, byte* pc)
      : pos_(pos),
        last_pc_(pc),
        last_id_(0),
        last_position_(0),
        last_mode_(RelocInfo::NUMBER_OF_MODES),
        next_position_candidate_pos_delta_(0),
        next_position_candidate_pc_delta_(0),
        next_position_candidate_flushed_(true) {}

  byte* pos() const { return pos_; }
  byte* last_pc() const { return last_pc_; }

  void Write(const RelocInfo* rinfo);

  // Update the state of the stream after reloc info buffer
  // and/or code is moved while the stream is active.
  void Reposition(byte* pos, byte* pc) {
    pos_ = pos;
    last_pc_ = pc;
  }

  void Finish() { FlushPosition(); }

  // Max size (bytes) of a written RelocInfo. Longest encoding is
  // ExtraTag, VariableLengthPCJump, ExtraTag, pc_delta, data_delta.
  // On ia32 and arm this is 1 + 4 + 1 + 1 + 4 = 11.
  // On x64 this is 1 + 4 + 1 + 1 + 8 == 15;
  // Here we use the maximum of the two.
  static const int kMaxSize = 15;

 private:
  inline uint32_t WriteLongPCJump(uint32_t pc_delta);

  inline void WriteShortTaggedPC(uint32_t pc_delta, int tag);
  inline void WriteShortTaggedData(intptr_t data_delta, int tag);

  inline void WriteMode(RelocInfo::Mode rmode);
  inline void WriteModeAndPC(uint32_t pc_delta, RelocInfo::Mode rmode);
  inline void WriteIntData(int data_delta);
  inline void WriteData(intptr_t data_delta);
  inline void WritePosition(int pc_delta, int pos_delta, RelocInfo::Mode rmode);

  void FlushPosition();

  byte* pos_;
  byte* last_pc_;
  int last_id_;
  int last_position_;
  RelocInfo::Mode last_mode_;
  int next_position_candidate_pos_delta_;
  uint32_t next_position_candidate_pc_delta_;
  bool next_position_candidate_flushed_;

  DISALLOW_COPY_AND_ASSIGN(RelocInfoWriter);
};


// A RelocIterator iterates over relocation information.
// Typical use:
//
//   for (RelocIterator it(code); !it.done(); it.next()) {
//     // do something with it.rinfo() here
//   }
//
// A mask can be specified to skip unwanted modes.
class RelocIterator: public Malloced {
 public:
  // Create a new iterator positioned at
  // the beginning of the reloc info.
  // Relocation information with mode k is included in the
  // iteration iff bit k of mode_mask is set.
  explicit RelocIterator(Code* code, int mode_mask = -1);
  explicit RelocIterator(const CodeDesc& desc, int mode_mask = -1);

  // Iteration
  bool done() const { return done_; }
  void next();

  // Return pointer valid until next next().
  RelocInfo* rinfo() {
    DCHECK(!done());
    return &rinfo_;
  }

 private:
  // Advance* moves the position before/after reading.
  // *Read* reads from current byte(s) into rinfo_.
  // *Get* just reads and returns info on current byte.
  void Advance(int bytes = 1) { pos_ -= bytes; }
  int AdvanceGetTag();
  RelocInfo::Mode GetMode();

  void AdvanceReadLongPCJump();

  int GetShortDataTypeTag();
  void ReadShortTaggedPC();
  void ReadShortTaggedId();
  void ReadShortTaggedPosition();
  void ReadShortTaggedData();

  void AdvanceReadPC();
  void AdvanceReadId();
  void AdvanceReadInt();
  void AdvanceReadPosition();
  void AdvanceReadData();

  // If the given mode is wanted, set it in rinfo_ and return true.
  // Else return false. Used for efficiently skipping unwanted modes.
  bool SetMode(RelocInfo::Mode mode) {
    return (mode_mask_ & (1 << mode)) ? (rinfo_.rmode_ = mode, true) : false;
  }

  byte* pos_;
  byte* end_;
  byte* code_age_sequence_;
  RelocInfo rinfo_;
  bool done_;
  int mode_mask_;
  int last_id_;
  int last_position_;
  DISALLOW_COPY_AND_ASSIGN(RelocIterator);
};


//------------------------------------------------------------------------------
// External function

//----------------------------------------------------------------------------
class SCTableReference;
class Debug_Address;


// An ExternalReference represents a C++ address used in the generated
// code. All references to C++ functions and variables must be encapsulated in
// an ExternalReference instance. This is done in order to track the origin of
// all external references in the code so that they can be bound to the correct
// addresses when deserializing a heap.
class ExternalReference BASE_EMBEDDED {
 public:
  // Used in the simulator to support different native api calls.
  enum Type {
    // Builtin call.
    // Object* f(v8::internal::Arguments).
    BUILTIN_CALL,  // default

    // Builtin that takes float arguments and returns an int.
    // int f(double, double).
    BUILTIN_COMPARE_CALL,

    // Builtin call that returns floating point.
    // double f(double, double).
    BUILTIN_FP_FP_CALL,

    // Builtin call that returns floating point.
    // double f(double).
    BUILTIN_FP_CALL,

    // Builtin call that returns floating point.
    // double f(double, int).
    BUILTIN_FP_INT_CALL,

    // Direct call to API function callback.
    // void f(v8::FunctionCallbackInfo&)
    DIRECT_API_CALL,

    // Call to function callback via InvokeFunctionCallback.
    // void f(v8::FunctionCallbackInfo&, v8::FunctionCallback)
    PROFILING_API_CALL,

    // Direct call to accessor getter callback.
    // void f(Local<Name> property, PropertyCallbackInfo& info)
    DIRECT_GETTER_CALL,

    // Call to accessor getter callback via InvokeAccessorGetterCallback.
    // void f(Local<Name> property, PropertyCallbackInfo& info,
    //     AccessorNameGetterCallback callback)
    PROFILING_GETTER_CALL
  };

  static void SetUp();
  static void InitializeMathExpData();
  static void TearDownMathExpData();

  typedef void* ExternalReferenceRedirector(void* original, Type type);

  ExternalReference() : address_(NULL) {}

  ExternalReference(Builtins::CFunctionId id, Isolate* isolate);

  ExternalReference(ApiFunction* ptr, Type type, Isolate* isolate);

  ExternalReference(Builtins::Name name, Isolate* isolate);

  ExternalReference(Runtime::FunctionId id, Isolate* isolate);

  ExternalReference(const Runtime::Function* f, Isolate* isolate);

  explicit ExternalReference(StatsCounter* counter);

  ExternalReference(Isolate::AddressId id, Isolate* isolate);

  explicit ExternalReference(const SCTableReference& table_ref);

  // Isolate as an external reference.
  static ExternalReference isolate_address(Isolate* isolate);

  // One-of-a-kind references. These references are not part of a general
  // pattern. This means that they have to be added to the
  // ExternalReferenceTable in serialize.cc manually.

  static ExternalReference incremental_marking_record_write_function(
      Isolate* isolate);
  static ExternalReference store_buffer_overflow_function(
      Isolate* isolate);
  static ExternalReference delete_handle_scope_extensions(Isolate* isolate);

  static ExternalReference get_date_field_function(Isolate* isolate);
  static ExternalReference date_cache_stamp(Isolate* isolate);

  static ExternalReference get_make_code_young_function(Isolate* isolate);
  static ExternalReference get_mark_code_as_executed_function(Isolate* isolate);

  // Deoptimization support.
  static ExternalReference new_deoptimizer_function(Isolate* isolate);
  static ExternalReference compute_output_frames_function(Isolate* isolate);

  // Log support.
  static ExternalReference log_enter_external_function(Isolate* isolate);
  static ExternalReference log_leave_external_function(Isolate* isolate);

  // Static data in the keyed lookup cache.
  static ExternalReference keyed_lookup_cache_keys(Isolate* isolate);
  static ExternalReference keyed_lookup_cache_field_offsets(Isolate* isolate);

  // Static variable Heap::roots_array_start()
  static ExternalReference roots_array_start(Isolate* isolate);

  // Static variable Heap::allocation_sites_list_address()
  static ExternalReference allocation_sites_list_address(Isolate* isolate);

  // Static variable StackGuard::address_of_jslimit()
  static ExternalReference address_of_stack_limit(Isolate* isolate);

  // Static variable StackGuard::address_of_real_jslimit()
  static ExternalReference address_of_real_stack_limit(Isolate* isolate);

  // Static variable RegExpStack::limit_address()
  static ExternalReference address_of_regexp_stack_limit(Isolate* isolate);

  // Static variables for RegExp.
  static ExternalReference address_of_static_offsets_vector(Isolate* isolate);
  static ExternalReference address_of_regexp_stack_memory_address(
      Isolate* isolate);
  static ExternalReference address_of_regexp_stack_memory_size(
      Isolate* isolate);

  // Static variable Heap::NewSpaceStart()
  static ExternalReference new_space_start(Isolate* isolate);
  static ExternalReference new_space_mask(Isolate* isolate);

  // Write barrier.
  static ExternalReference store_buffer_top(Isolate* isolate);

  // Used for fast allocation in generated code.
  static ExternalReference new_space_allocation_top_address(Isolate* isolate);
  static ExternalReference new_space_allocation_limit_address(Isolate* isolate);
  static ExternalReference old_space_allocation_top_address(Isolate* isolate);
  static ExternalReference old_space_allocation_limit_address(Isolate* isolate);

  static ExternalReference mod_two_doubles_operation(Isolate* isolate);
  static ExternalReference power_double_double_function(Isolate* isolate);
  static ExternalReference power_double_int_function(Isolate* isolate);

  static ExternalReference handle_scope_next_address(Isolate* isolate);
  static ExternalReference handle_scope_limit_address(Isolate* isolate);
  static ExternalReference handle_scope_level_address(Isolate* isolate);

  static ExternalReference scheduled_exception_address(Isolate* isolate);
  static ExternalReference address_of_pending_message_obj(Isolate* isolate);

  // Static variables containing common double constants.
  static ExternalReference address_of_min_int();
  static ExternalReference address_of_one_half();
  static ExternalReference address_of_minus_one_half();
  static ExternalReference address_of_negative_infinity();
  static ExternalReference address_of_the_hole_nan();
  static ExternalReference address_of_uint32_bias();

  static ExternalReference math_log_double_function(Isolate* isolate);

  static ExternalReference math_exp_constants(int constant_index);
  static ExternalReference math_exp_log_table();

  static ExternalReference page_flags(Page* page);

  static ExternalReference ForDeoptEntry(Address entry);

  static ExternalReference cpu_features();

  static ExternalReference debug_is_active_address(Isolate* isolate);
  static ExternalReference debug_after_break_target_address(Isolate* isolate);
  static ExternalReference debug_restarter_frame_function_pointer_address(
      Isolate* isolate);

  static ExternalReference is_profiling_address(Isolate* isolate);
  static ExternalReference invoke_function_callback(Isolate* isolate);
  static ExternalReference invoke_accessor_getter_callback(Isolate* isolate);

  static ExternalReference vector_store_virtual_register(Isolate* isolate);

  static ExternalReference runtime_function_table_address(Isolate* isolate);

  Address address() const { return reinterpret_cast<Address>(address_); }

  // Used to check if single stepping is enabled in generated code.
  static ExternalReference debug_step_in_fp_address(Isolate* isolate);

#ifndef V8_INTERPRETED_REGEXP
  // C functions called from RegExp generated code.

  // Function NativeRegExpMacroAssembler::CaseInsensitiveCompareUC16()
  static ExternalReference re_case_insensitive_compare_uc16(Isolate* isolate);

  // Function RegExpMacroAssembler*::CheckStackGuardState()
  static ExternalReference re_check_stack_guard_state(Isolate* isolate);

  // Function NativeRegExpMacroAssembler::GrowStack()
  static ExternalReference re_grow_stack(Isolate* isolate);

  // byte NativeRegExpMacroAssembler::word_character_bitmap
  static ExternalReference re_word_character_map();

#endif

  // This lets you register a function that rewrites all external references.
  // Used by the ARM simulator to catch calls to external references.
  static void set_redirector(Isolate* isolate,
                             ExternalReferenceRedirector* redirector) {
    // We can't stack them.
    DCHECK(isolate->external_reference_redirector() == NULL);
    isolate->set_external_reference_redirector(
        reinterpret_cast<ExternalReferenceRedirectorPointer*>(redirector));
  }

  static ExternalReference stress_deopt_count(Isolate* isolate);

  static ExternalReference fixed_typed_array_base_data_offset();

 private:
  explicit ExternalReference(void* address)
      : address_(address) {}

  static void* Redirect(Isolate* isolate,
                        Address address_arg,
                        Type type = ExternalReference::BUILTIN_CALL) {
    ExternalReferenceRedirector* redirector =
        reinterpret_cast<ExternalReferenceRedirector*>(
            isolate->external_reference_redirector());
    void* address = reinterpret_cast<void*>(address_arg);
    void* answer = (redirector == NULL) ?
                   address :
                   (*redirector)(address, type);
    return answer;
  }

  void* address_;
};

bool operator==(ExternalReference, ExternalReference);
bool operator!=(ExternalReference, ExternalReference);

size_t hash_value(ExternalReference);

std::ostream& operator<<(std::ostream&, ExternalReference);


// -----------------------------------------------------------------------------
// Position recording support

struct PositionState {
  PositionState() : current_position(RelocInfo::kNoPosition),
                    written_position(RelocInfo::kNoPosition),
                    current_statement_position(RelocInfo::kNoPosition),
                    written_statement_position(RelocInfo::kNoPosition) {}

  int current_position;
  int written_position;

  int current_statement_position;
  int written_statement_position;
};


class PositionsRecorder BASE_EMBEDDED {
 public:
  explicit PositionsRecorder(Assembler* assembler)
      : assembler_(assembler) {
    jit_handler_data_ = NULL;
  }

  void AttachJITHandlerData(void* user_data) {
    jit_handler_data_ = user_data;
  }

  void* DetachJITHandlerData() {
    void* old_data = jit_handler_data_;
    jit_handler_data_ = NULL;
    return old_data;
  }
  // Set current position to pos.
  void RecordPosition(int pos);

  // Set current statement position to pos.
  void RecordStatementPosition(int pos);

  // Write recorded positions to relocation information.
  bool WriteRecordedPositions();

  int current_position() const { return state_.current_position; }

  int current_statement_position() const {
    return state_.current_statement_position;
  }

 private:
  Assembler* assembler_;
  PositionState state_;

  // Currently jit_handler_data_ is used to store JITHandler-specific data
  // over the lifetime of a PositionsRecorder
  void* jit_handler_data_;

  DISALLOW_COPY_AND_ASSIGN(PositionsRecorder);
};


// -----------------------------------------------------------------------------
// Utility functions

inline int NumberOfBitsSet(uint32_t x) {
  unsigned int num_bits_set;
  for (num_bits_set = 0; x; x >>= 1) {
    num_bits_set += x & 1;
  }
  return num_bits_set;
}

bool EvalComparison(Token::Value op, double op1, double op2);

// Computes pow(x, y) with the special cases in the spec for Math.pow.
double power_helper(double x, double y);
double power_double_int(double x, int y);
double power_double_double(double x, double y);

// Helper class for generating code or data associated with the code
// right after a call instruction. As an example this can be used to
// generate safepoint data after calls for crankshaft.
class CallWrapper {
 public:
  CallWrapper() { }
  virtual ~CallWrapper() { }
  // Called just before emitting a call. Argument is the size of the generated
  // call code.
  virtual void BeforeCall(int call_size) const = 0;
  // Called just after emitting a call, i.e., at the return site for the call.
  virtual void AfterCall() const = 0;
};

class NullCallWrapper : public CallWrapper {
 public:
  NullCallWrapper() { }
  virtual ~NullCallWrapper() { }
  virtual void BeforeCall(int call_size) const { }
  virtual void AfterCall() const { }
};


// -----------------------------------------------------------------------------
// Constant pool support

class ConstantPoolEntry {
 public:
  ConstantPoolEntry() {}
  ConstantPoolEntry(int position, intptr_t value, bool sharing_ok)
      : position_(position),
        merged_index_(sharing_ok ? SHARING_ALLOWED : SHARING_PROHIBITED),
        value_(value) {}
  ConstantPoolEntry(int position, double value)
      : position_(position), merged_index_(SHARING_ALLOWED), value64_(value) {}

  int position() const { return position_; }
  bool sharing_ok() const { return merged_index_ != SHARING_PROHIBITED; }
  bool is_merged() const { return merged_index_ >= 0; }
  int merged_index(void) const {
    DCHECK(is_merged());
    return merged_index_;
  }
  void set_merged_index(int index) {
    merged_index_ = index;
    DCHECK(is_merged());
  }
  int offset(void) const {
    DCHECK(merged_index_ >= 0);
    return merged_index_;
  }
  void set_offset(int offset) {
    DCHECK(offset >= 0);
    merged_index_ = offset;
  }
  intptr_t value() const { return value_; }
  uint64_t value64() const { return bit_cast<uint64_t>(value64_); }

  enum Type { INTPTR, DOUBLE, NUMBER_OF_TYPES };

  static int size(Type type) {
    return (type == INTPTR) ? kPointerSize : kDoubleSize;
  }

  enum Access { REGULAR, OVERFLOWED };

 private:
  int position_;
  int merged_index_;
  union {
    intptr_t value_;
    double value64_;
  };
  enum { SHARING_PROHIBITED = -2, SHARING_ALLOWED = -1 };
};


// -----------------------------------------------------------------------------
// Embedded constant pool support

class ConstantPoolBuilder BASE_EMBEDDED {
 public:
  ConstantPoolBuilder(int ptr_reach_bits, int double_reach_bits);

  // Add pointer-sized constant to the embedded constant pool
  ConstantPoolEntry::Access AddEntry(int position, intptr_t value,
                                     bool sharing_ok) {
    ConstantPoolEntry entry(position, value, sharing_ok);
    return AddEntry(entry, ConstantPoolEntry::INTPTR);
  }

  // Add double constant to the embedded constant pool
  ConstantPoolEntry::Access AddEntry(int position, double value) {
    ConstantPoolEntry entry(position, value);
    return AddEntry(entry, ConstantPoolEntry::DOUBLE);
  }

  // Previews the access type required for the next new entry to be added.
  ConstantPoolEntry::Access NextAccess(ConstantPoolEntry::Type type) const;

  bool IsEmpty() {
    return info_[ConstantPoolEntry::INTPTR].entries.empty() &&
           info_[ConstantPoolEntry::INTPTR].shared_entries.empty() &&
           info_[ConstantPoolEntry::DOUBLE].entries.empty() &&
           info_[ConstantPoolEntry::DOUBLE].shared_entries.empty();
  }

  // Emit the constant pool.  Invoke only after all entries have been
  // added and all instructions have been emitted.
  // Returns position of the emitted pool (zero implies no constant pool).
  int Emit(Assembler* assm);

  // Returns the label associated with the start of the constant pool.
  // Linking to this label in the function prologue may provide an
  // efficient means of constant pool pointer register initialization
  // on some architectures.
  inline Label* EmittedPosition() { return &emitted_label_; }

 private:
  ConstantPoolEntry::Access AddEntry(ConstantPoolEntry& entry,
                                     ConstantPoolEntry::Type type);
  void EmitSharedEntries(Assembler* assm, ConstantPoolEntry::Type type);
  void EmitGroup(Assembler* assm, ConstantPoolEntry::Access access,
                 ConstantPoolEntry::Type type);

  struct PerTypeEntryInfo {
    PerTypeEntryInfo() : regular_count(0), overflow_start(-1) {}
    bool overflow() const {
      return (overflow_start >= 0 &&
              overflow_start < static_cast<int>(entries.size()));
    }
    int regular_reach_bits;
    int regular_count;
    int overflow_start;
    std::vector<ConstantPoolEntry> entries;
    std::vector<ConstantPoolEntry> shared_entries;
  };

  Label emitted_label_;  // Records pc_offset of emitted pool
  PerTypeEntryInfo info_[ConstantPoolEntry::NUMBER_OF_TYPES];
};

}  // namespace internal
}  // namespace v8
#endif  // V8_ASSEMBLER_H_
