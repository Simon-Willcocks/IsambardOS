// Copyright (c) 2000 Simon Willcocks
// This is not intended to be much of a parser, everything has to be in the right format.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

typedef unsigned uint32_t;

// crc32 code taken from https://create.stephan-brumme.com/crc32/

const uint32_t Polynomial = 0xEDB88320;

uint32_t crc32(const void* data, size_t length, uint32_t previousCrc32)
{
  uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
  unsigned char* current = (unsigned char*) data;
  while (length--) {
    crc ^= *current++;
    for (unsigned int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & Polynomial);
    }
  }
  return ~crc; // same as crc ^ 0xFFFFFFFF
}

char *lines[1000];
int interfaces[100];
int ends[100];

int last_line = 0;
int last_interface = 0;
int last_end = 0;

const char *routine_name( char *routine )
{
  static char result[100];
  sscanf( routine, "%[a-zA-Z0-9_]", result );
  return result;
}

void export_typeset( char *type_set )
{
  printf( " %s ", type_set );
}

void export_return_type( char *routine )
{
  char *outparams = strstr( routine, " OUT " );
  if (outparams == 0) {
    printf( "void " );
  }
  else {
    outparams += 5;

    char name[32];
    char type_set[128];

    int matched = sscanf( outparams, "%[a-zA-Z0-9_]: %[a-zA-Z0-9_&], ", name, type_set );

    if (2 == matched) {
      printf( "struct { " );
      while (2 == matched) {
        export_typeset( type_set );
        printf( " %s; ", name );
        outparams = strstr( outparams+1, "," );
        if (outparams == 0) {
          matched = 0;
        }
        else {
          matched = sscanf( outparams, ", %[a-zA-Z0-9_]: %[a-zA-Z0-9_&], ", name, type_set );
        }
      }
      export_typeset( type_set );
      printf( " %s; } ", name );
    }
    else {
  	  printf( "   *** %d ***   ", matched );
      export_typeset( type_set );
    }
  }
}

int parameters_count( char *parameters )
{
  char *p = parameters;
  int result = 0;
  while (p != 0) {
    result ++;
    p = strchr( p+1, ',' );
  }
  return result;
}

void print_parameter_type( char *p )
{
  char name[32];
  char type_set[128];

  int matched = sscanf( p, " %[a-zA-Z0-9_]: %[a-zA-Z0-9_&]", name, type_set );
  if (matched != 2) { exit( 1 ); }
  printf( "%s", type_set );
}

void print_parameter_name( char *p )
{
  char name[32];

  int matched = sscanf( p, " %[a-zA-Z0-9_]:", name );
  if (matched != 1) { exit( 1 ); }
  printf( "%s", name );
}

void print_parameter_decl( char *p )
{
  char name[32];
  char type_set[128];

  int matched = sscanf( p, " %[a-zA-Z0-9_]: %[a-zA-Z0-9_&]", name, type_set );
  if (matched != 2) { exit( 1 ); }
  printf( "%s %s", type_set, name );
}

static uint32_t printed_types[100] = { 0 };

void print_typedef( char *type )
{
  unsigned crc = crc32( type, strlen( type ), 0 );
  uint32_t *p = printed_types;
  while (*p != crc && *p != 0) { p++; }
  if (*p == 0) {
    printf( "#ifndef %s_DEFINED\n", type );
    printf( "#define %s_DEFINED\n", type );
    printf( "typedef struct { integer_register r; } %s;\n", type );
    printf( "static inline %s %s_from_integer_register( integer_register r ) { %s result = { .r = r }; return result; }\n", type, type, type );
    printf( "#endif\n" );
    if (p - printed_types >= (sizeof( printed_types ) / sizeof( printed_types[0] )-1)) {
      printf( "// Too many unique types, modify tools/interfaces.c!\n" );
    }
    else {
      *p = crc;
    }
  }
}

void print_decl( char *p )
{
  char type_set[128];

  int matched = sscanf( p, " %[a-zA-Z0-9_&]", type_set );
  if (matched != 1) { exit( 1 ); }
  if (strcmp( type_set, "NUMBER" ) != 0)
    print_typedef( type_set );
}

typedef void (*routine_parser)( const char *interface_name, const char *name, unsigned crc, char *in, char *out );

static inline void split( const char *interface_name, char *routine, routine_parser fn, unsigned prefix_crc )
{
  unsigned crc = crc32( routine, strlen( routine ), prefix_crc );

  char *in = 0, *out = 0;

  // Ordering of these is important: last section first.
  char *outparams = strstr( routine, " OUT " );
  if (outparams != 0) { *outparams = '\0'; out = outparams + 5; }

  char *inparams = strstr( routine, " IN " );
  if (inparams != 0) { *inparams = '\0'; in = inparams + 4; }

  char *first_space = strstr( routine, " " );
  if (first_space != 0) { *first_space = '\0'; }

  fn( interface_name, routine, crc, in, out );

  if (first_space != 0) { *first_space = ' '; }
  if (outparams != 0) { *outparams = ' '; }
  if (inparams != 0) { *inparams = ' '; }
}

void export_interface_routine_client_code( const char *interface_name, const char *name, unsigned crc, char *in, char *out )
{
  printf( "static inline " );

  int out_params = parameters_count( out );

  switch (out_params) {
  case 0: printf( "void" ); break;
  case 1: print_parameter_type( out ); break;
  default: printf( "TODO " );
  }

  printf( " %s__%s( %s o", interface_name, name, interface_name );

  int in_params = parameters_count( in );
  char *p = in;
  for (int i = 0; i < in_params; i++) {
    printf( ", " );
    print_parameter_decl( p );
    p = strchr( p, ',' ) + 1;
  }

  printf( " )\n{\n" );
  if (out_params > 0) {
    printf( "  return " );
    print_parameter_type( out );
    printf( "_from_integer_register( Isambard_" );
  }
  else {
    printf( "  Isambard_" );
  }
  printf( "%d%d( o.r, 0x%08x", in_params, out_params, crc );

  p = in;
  for (int i = 0; i < in_params; i++) {
    printf( ", " );
    print_parameter_name( p );
    printf( ".r" );
    p = strchr( p, ',' ) + 1;
  }
  if (out_params > 0) {
    printf( " ) );\n" );
  }
  else {
    printf( " );\n" );
  }

  printf( "}\n\n" );
}

void declare_listed_interfaces( char *list )
{
  char *p = list;
  int count = parameters_count( list );
  for (int i = 0; i < count; i++) {
    p = strchr( p, ':' ) + 1;
    print_decl( p );
    p = strchr( p, ',' ) + 1;
  }
}

void declare_required_interfaces( const char *interface_name, const char *name, unsigned crc, char *in, char *out )
{
  declare_listed_interfaces( in );
  declare_listed_interfaces( out );
}

void export_client_code()
{
#if 0
  for (int i = 0; i < last_interface; i++) {
    int l = interfaces[i];
    char *interface_name = lines[l++] + 10;
    print_typedef( interface_name );
  }

  for (int i = 0; i < last_interface; i++) {
    int l = interfaces[i];
    char *interface_name = lines[l++] + 10;
    print_typedef( interface_name );

    while (l < ends[i]) {
      split( interface_name, lines[l++], declare_required_interfaces, 0 );
    }
  }
#endif

  for (int i = 0; i < last_interface; i++) {
    int l = interfaces[i];
    char *interface_name = lines[l++] + 10;

    unsigned crc = crc32( interface_name, strlen( interface_name ), 0 );
    crc = crc32( "__", 2, crc );

    while (l < ends[i]) {
      split( interface_name, lines[l++], export_interface_routine_client_code, crc );
    }
  }
}

void export_interface_routine_server_code( const char *interface_name, const char *name, unsigned crc, char *in, char *out )
{
  int out_params = parameters_count( out );

  switch (out_params) {
  case 0: printf( "void" ); break;
  case 1: print_parameter_type( out ); break;
  default: printf( "TODO" );
  }

  printf( " c##__%s__%s( c o", interface_name, name );

  int in_params = parameters_count( in );
  char *p = in;
  for (int i = 0; i < in_params; i++) {
    printf( ", " );
    print_parameter_decl( p );
    p = strchr( p, ',' ) + 1;
  }

  printf( " ); \\\n" );
}

void export_interface_routine_case_code( const char *interface_name, const char *name, unsigned crc, char *in, char *out )
{
  printf( "  case 0x%08x: ", crc );
  if (out) printf( "return " );
  printf( "c##__%s__%s( o", interface_name, name );

  int in_params = parameters_count( in );
  char *p = in;
  for (int i = 0; i < in_params; i++) {
    printf( ", " );
    print_parameter_type( p );
    printf( "_from_integer_register( p%d )", i + 1 );
    p = strchr( p, ',' ) + 1;
  }
  if (!out)
    printf( " ); return 0;" );
  else
    printf( " ).r;" );
  printf( " \\\n" );
}

void export_server_code()
{
  for (int i = 0; i < last_interface; i++) {
    int l = interfaces[i];
    char *interface_name = lines[l++] + 10;

    unsigned crc = crc32( interface_name, strlen( interface_name ), 0 );
    crc = crc32( "__", 2, crc );

    printf( "#define ISAMBARD_%s__SERVER( c ) \\\n", interface_name );
    while (l < ends[i]) {
      split( interface_name, lines[l++], export_interface_routine_server_code, crc );
    }

    // Only single return values supported.
    printf( "integer_register c##_call_handler( c o, integer_register call, integer_register p1, integer_register p2, integer_register p3 ); \\\n" );
    printf( "extern void c##_veneer(); \\\n" ); // Implementation provided by a macro from isambard_client.h

    printf( "static inline %s c##_%s_duplicate_to_return( c o ) { %s result; result.r = duplicate_to_return( (integer_register) o.r ); return result; } \\\n", interface_name, interface_name, interface_name );
    printf( "static inline %s c##_%s_duplicate_to_pass_to( integer_register target, c o ) { %s result; result.r = duplicate_to_pass_to( target, (integer_register) o.r ); return result; } \\\n", interface_name, interface_name, interface_name );
    printf( "static inline %s c##_%s_to_return( c o ) { %s result; result.r = object_to_return( c##_veneer, (integer_register) o.r ); return result; } \\\n", interface_name, interface_name, interface_name );
    printf( "static inline %s c##_%s_to_pass_to( integer_register target, c o ) { %s result; result.r = object_to_pass_to( target, c##_veneer, (integer_register) o.r ); return result; }\n\n", interface_name, interface_name, interface_name );

    l = interfaces[i] + 1;

    printf( "#define AS_%s( c )  switch (call) {\\\n", interface_name );
    while (l < ends[i]) {
      split( interface_name, lines[l++], export_interface_routine_case_code, crc );
    }
    printf( "  }\n" );
  }
}

// Server code.
// Concrete type
//   Pointer, or value
// Stack top
//   Per object, per driver, per thread?
//   Recursion allowed?
//     If so, the SP must be stored (near the lock?), on outgoing calls...
//     There will be problems, if the thread can move between cores, at least with the system driver.
// Lock
//   Per object, per driver, per stack?
// More than one interface provided?
//   Provide a switch statement for each interface; they can be included in the handler function
//
// The implementation must provide an entry point for the call handler, this will perform a lock,
// setup the stack pointer, and call the C call handler routine for the type.
// This is independent of any interfaces it provides.
//     PER_OBJECT_ENTRY( type ), or STACK_POOL_ENTRY( type, size, count )...?
//
// The C call handler routine can support multiple interfaces provided by the type.
//     ISAMBARD_PROVIDER( type, type_AS_interface1; type_AS_interface2... )?
//
// Use cases:
// 1. Map: can be used by any core, any thread, no recursion, so single stack
int main( int argc, char *argv[] )
{
  void (*generator)() = 0;
  const char *program = basename( argv[0] );

  if (0 == strcmp( program, "client")) {
    generator = export_client_code;
  }
  if (0 == strcmp( program, "server")) {
    generator = export_server_code;
  }
  if (0 == generator) {
    fprintf( stderr, "Unknown program name\n" );
    return 1;
  }

  struct stat statbuf;

  if (0 != stat(argv[1], &statbuf)) {
    perror( "Getting size of file" );
    return 1;
  }

  int file_size = statbuf.st_size;
  char *file = malloc( file_size + 1 );
  int fd = open( argv[1], O_RDONLY );
  if (fd < 0) {
    perror( "Opening file" );
  }
  else {
    if (read( fd, file, file_size ) != file_size) {
      perror( "Reading file" );
    }
    close( fd );
  }

  file[file_size] = '\0';

  // strtok expects a pointer on the first call, NULL on subsequent calls
  // strtok overwrites the delimiter with a nul character, and doesn't restore it on the next call
  char *string = file;

  while (lines[last_line] = strtok( string, "\n" )) {
    string = 0; // For second and subsequent calls to strtok
    if (strlen( lines[last_line] ) == 0) continue;

    if (0 == strncmp( lines[last_line], "interface ", 10)) {
      interfaces[last_interface++] = last_line;
      if (last_interface > last_end+1) return 1;
    }
    else if (0 == strcmp( lines[last_line], "end" )) {
      ends[last_end++] = last_line;
      if (last_interface != last_end) return 1;
    }

    last_line++;
  }

  // Now, each entry in lines contains a pointer to a non-empty string
  // Each entry in interfaces contains the line number of a line starting "interface "
  // Each entry in ends contains the line number of a line containing "end"

  if (last_end != last_interface) return 1;

  generator();

  return 0;
}
