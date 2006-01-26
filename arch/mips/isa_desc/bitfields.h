////////////////////////////////////////////////////////////////////
//
// Bitfield definitions.
//

def bitfield OPCODE_HI  <31:29>;
def bitfield OPCODE_LO  <28:26>;

def bitfield FUNCTION_HI   < 5: 3>;
def bitfield FUNCTION_LO   < 2: 0>;

def bitfield RT	      <20:16>;
def bitfield RT_HI    <20:19>;
def bitfield RT_LO    <18:16>;

def bitfield RS	      <25:21>;
def bitfield RS_HI    <25:24>;
def bitfield RS_LO    <23:21>;

def bitfield RD	      <15:11>;

// Floating-point operate format
def bitfield FMT	  <25:21>;
def bitfield FT	      <20:16>;
def bitfield FS	      <15:11>;
def bitfield FD	      <10:6>;

def bitfield MOVCI    <16:16>;
def bitfield MOVCF    <16:16>;
def bitfield SRL      <21:21>;
def bitfield SRLV     < 6: 6>;
def bitfield SA       <10: 6>;

// Interrupts
def bitfield SC       < 5: 5>;

// Integer operate format(s>;
def bitfield INTIMM	<15: 0>; // integer immediate (literal)

// Branch format
def bitfield OFFSET <15: 0>; // displacement

// Memory-format jumps
def bitfield JMPTARG	<25: 0>;
def bitfield JMPHINT	<10: 6>;

def bitfield SYSCALLCODE <25: 6>;
def bitfield TRAPCODE    <15:13>;

// M5 instructions
def bitfield M5FUNC <7:0>;
