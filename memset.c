
void *memset(void *s, int c, long unsigned int n)
{
  char *p = s;
  for (unsigned long int i = 0; i < n; i++) {
    p[i] = c;
  }
  return s;
}


