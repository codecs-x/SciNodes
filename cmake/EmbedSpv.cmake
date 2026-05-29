# EmbedSpv.cmake — wraps a SPIR-V binary as a C++ header with a
# `static const uint32_t <SYM>[] = { ... };` array.
#
# Inputs (passed via -D):
#   IN   absolute path to the .spv binary
#   OUT  absolute path to the header to generate
#   SYM  symbol name to expose

file(READ "${IN}" SPV_HEX HEX)

# Convert the hex stream into 0x........ tokens.
string(LENGTH "${SPV_HEX}" HEX_LEN)
set(LINE "")
set(BODY "")
math(EXPR LAST "${HEX_LEN} - 1")
set(COUNT 0)
math(EXPR WORDS_DIV4 "${HEX_LEN} / 8")
set(WORDS "")
set(I 0)
while(I LESS ${HEX_LEN})
  string(SUBSTRING "${SPV_HEX}" ${I} 8 WORD)
  # The on-disk byte order is little-endian; reverse the 4 bytes so the
  # numeric value matches the SPIR-V word.
  string(SUBSTRING "${WORD}" 6 2 B0)
  string(SUBSTRING "${WORD}" 4 2 B1)
  string(SUBSTRING "${WORD}" 2 2 B2)
  string(SUBSTRING "${WORD}" 0 2 B3)
  set(W "0x${B0}${B1}${B2}${B3}")
  list(APPEND WORDS "${W}")
  math(EXPR I "${I} + 8")
endwhile()

list(LENGTH WORDS WORD_COUNT)
list(JOIN WORDS ", " BODY)

file(APPEND "${OUT}" "#include <cstdint>\n")
file(APPEND "${OUT}" "static const uint32_t ${SYM}[] = {\n  ${BODY}\n};\n")
file(APPEND "${OUT}" "static const unsigned ${SYM}_count = ${WORD_COUNT};\n")
