/*

  VSEARCH: a versatile open source tool for metagenomics

  Copyright (C) 2014-2015, Torbjorn Rognes, Frederic Mahe and Tomas Flouri
  All rights reserved.

  Contact: Torbjorn Rognes <torognes@ifi.uio.no>,
  Department of Informatics, University of Oslo,
  PO Box 1080 Blindern, NO-0316 Oslo, Norway

  This software is dual-licensed and available under a choice
  of one of two licenses, either under the terms of the GNU
  General Public License version 3 or the BSD 2-Clause License.


  GNU General Public License version 3

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.


  The BSD 2-Clause License

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

*/

#include "vsearch.h"

void fastq_fatal(unsigned long lineno, const char * msg)
{
  char * string; 
  if (asprintf(& string,
               "Invalid line %lu in FASTQ file: %s",
               lineno,
               msg) == -1)
    fatal("Out of memory");
  
  if (string)
    {
      fatal(string);
      free(string);
    }
  else
    fatal("Out of memory");
}

void buffer_filter_extend(fastx_handle h,
                          struct fastx_buffer_s * dest_buffer,
                          char * source_buf,
                          unsigned long len,
                          unsigned int * char_action,
                          const unsigned char * char_mapping,
                          unsigned long lineno_start)
{
  buffer_makespace(dest_buffer, len+1);

  /* Strip unwanted characters from the string and raise warnings or
     errors on certain characters. */

  unsigned long lineno = lineno_start;

  char * p = source_buf;
  char * d = dest_buffer->data + dest_buffer->length;
  char * q = d;
  char msg[200];

  for(unsigned long i = 0; i < len; i++)
    {
      char c = *p++;
      char m = char_action[(int)c];

      switch(m)
        {
        case 0:
          /* stripped */
          h->stripped_all++;
          h->stripped[(int)c]++;
          break;
              
        case 1:
          /* legal character */
          *q++ = char_mapping[(int)(c)];
          break;
          
        case 2:
          /* fatal character */
          if ((c>=32) && (c<127))
            snprintf(msg,
                     200,
                     "Illegal character '%c'",
                     c);
          else
            snprintf(msg,
                     200,
                     "Illegal unprintable character %#.2x (hexadecimal)",
                     c);
          fastq_fatal(lineno, msg);
          break;
              
        case 3:
          /* silently stripped chars (whitespace) */
          break;

        case 4:
          /* newline (silently stripped) */
          lineno++;
          break;
        }
    }

  /* add zero after sequence */
  *q = 0;
  dest_buffer->length += q - d;
}

fastx_handle fastq_open(const char * filename)
{
  fastx_handle h = fastx_open(filename);

  if (!fastx_is_fastq(h))
    fatal("FASTQ file expected, FASTA file found (%s)", filename);

  return h;
}

void fastq_close(fastx_handle h)
{
  fastx_close(h);
}

bool fastq_next(fastx_handle h,
                bool truncateatspace,
                const unsigned char * char_mapping)
{
  h->header_buffer.length = 0;
  h->header_buffer.data[0] = 0;
  h->sequence_buffer.length = 0;
  h->sequence_buffer.data[0] = 0;
  h->quality_buffer.length = 0;
  h->quality_buffer.data[0] = 0;

  h->lineno_start = h->lineno;

  unsigned long rest = fastx_file_fill_buffer(h);

  /* check end of file */

  if (rest == 0)
    return 0;

  /* read header */

  /* check initial @ character */
  
  if (h->file_buffer.data[h->file_buffer.position] != '@')
    fastq_fatal(h->lineno, "Header line must start with '@' character");
  h->file_buffer.position++;
  rest--;

  char * lf = 0;
  while (lf == 0)
    {
      /* get more data if buffer empty */
      rest = fastx_file_fill_buffer(h);
      if (rest == 0)
        fastq_fatal(h->lineno, "Unexpected end of file");
      
      /* find LF */
      lf = (char *) memchr(h->file_buffer.data + h->file_buffer.position,
                           '\n',
                           rest);

      /* copy to header buffer */
      unsigned long len = rest;
      if (lf)
        {
          /* LF found, copy up to and including LF */
          len = lf - (h->file_buffer.data + h->file_buffer.position) + 1;
          h->lineno++;
        }
      buffer_extend(& h->header_buffer,
                    h->file_buffer.data + h->file_buffer.position,
                    len);
      h->file_buffer.position += len;
      rest -= len;
    }

  unsigned long lineno_seq = h->lineno;

  /* read sequence line(s) */
  lf = 0;
  while (1)
    {
      /* get more data, if necessary */
      rest = fastx_file_fill_buffer(h);

      /* cannot end here */
      if (rest == 0)
        fastq_fatal(h->lineno, "Unexpected end of file");
      
      /* end when new line starting with + is seen */
      if (lf && (h->file_buffer.data[h->file_buffer.position] == '+'))
        break;

      /* find LF */
      lf = (char *) memchr(h->file_buffer.data + h->file_buffer.position,
                           '\n', rest);
      
      /* copy to sequence buffer */
      unsigned long len = rest;
      if (lf)
        {
          /* LF found, copy up to and including LF */
          len = lf - (h->file_buffer.data + h->file_buffer.position) + 1;
          h->lineno++;
        }

      buffer_filter_extend(h,
                           & h->sequence_buffer,
                           h->file_buffer.data + h->file_buffer.position,
                           len,
                           char_fq_action_seq, char_mapping, lineno_seq);
      h->file_buffer.position += len;
      rest -= len;
    }

#if 0
  if (h->sequence_buffer.length == 0)
    fastq_fatal(lineno_seq, "Empty sequence line");
#endif

  unsigned long lineno_plus = h->lineno;

  /* read + line */
  fastx_buffer_s plusline_buffer;
  buffer_init(&plusline_buffer);

  /* skip + character */
  h->file_buffer.position++;
  rest--;
  
  lf = 0;
  while (lf == 0)
    {
      /* get more data if buffer empty */
      rest = fastx_file_fill_buffer(h);

      /* cannot end here */
      if (rest == 0)
        fastq_fatal(h->lineno, "Unexpected end of file");
      
      /* find LF */
      lf = (char *) memchr(h->file_buffer.data + h->file_buffer.position,
                           '\n',
                           rest);
      /* copy to plusline buffer */
      unsigned long len = rest;
      if (lf)
        {
          /* LF found, copy up to and including LF */
          len = lf - (h->file_buffer.data + h->file_buffer.position) + 1;
          h->lineno++;
        }
      buffer_extend(& plusline_buffer,
                    h->file_buffer.data + h->file_buffer.position,
                    len);
      h->file_buffer.position += len;
      rest -= len;
    }

  /* check that the plus line is empty or identical to @ line */

  bool plusline_invalid = 0;
  if (h->header_buffer.length == plusline_buffer.length)
    {
      if (memcmp(h->header_buffer.data,
                 plusline_buffer.data,
                 h->header_buffer.length))
        plusline_invalid = 1;
    }
  else
    {
      if ((plusline_buffer.length > 2) ||
          ((plusline_buffer.length == 2) && (plusline_buffer.data[0] != '\r')))
        plusline_invalid = 1;
    }
  if (plusline_invalid)
    fastq_fatal(lineno_plus, 
                "'+' line must be empty or identical to header");

  buffer_free(&plusline_buffer);

  /* read quality line(s) */

  unsigned long lineno_qual = h->lineno;

  lf = 0;
  while (1)
    {
      /* get more data, if necessary */
      rest = fastx_file_fill_buffer(h);

      /* end if no more data */
      if (rest == 0)
        break;
      
      /* end if next entry starts : LF + '@' + correct length */
      if (lf && 
          (h->file_buffer.data[h->file_buffer.position] == '@') &&
          (h->quality_buffer.length == h->sequence_buffer.length))
        break;
      
      /* find LF */
      lf = (char *) memchr(h->file_buffer.data + h->file_buffer.position,
                           '\n', rest);
      
      /* copy to quality buffer */
      unsigned long len = rest;
      if (lf)
        {
          /* LF found, copy up to and including LF */
          len = lf - (h->file_buffer.data + h->file_buffer.position) + 1;
          h->lineno++;
        }
      buffer_filter_extend(h,
                           & h->quality_buffer,
                           h->file_buffer.data + h->file_buffer.position,
                           len,
                           char_fq_action_qual, chrmap_identity, lineno_qual);
      h->file_buffer.position += len;
      rest -= len;
    }

#if 0
  if (h->quality_buffer.length == 0)
    fastq_fatal(lineno_seq, "Empty quality line");
#endif

  if (h->sequence_buffer.length != h->quality_buffer.length)
    fastq_fatal(lineno_qual,
                "Sequence and quality lines must be equally long");

  buffer_truncate(& h->header_buffer, truncateatspace);

  h->seqno++;

  return 1;
}

char * fastq_get_quality(fastx_handle h)
{
  return h->quality_buffer.data;
}

unsigned long fastq_get_quality_length(fastx_handle h)
{
  return h->quality_buffer.length;
}

unsigned long fastq_get_position(fastx_handle h)
{
  return h->file_position;
}

unsigned long fastq_get_size(fastx_handle h)
{
  return h->file_size;
}

unsigned long fastq_get_lineno(fastx_handle h)
{
  return h->lineno_start;
}

unsigned long fastq_get_seqno(fastx_handle h)
{
  return h->seqno;
}

unsigned long fastq_get_header_length(fastx_handle h)
{
  return h->header_buffer.length;
}

unsigned long fastq_get_sequence_length(fastx_handle h)
{
  return h->sequence_buffer.length;
}

char * fastq_get_header(fastx_handle h)
{
  return h->header_buffer.data;
}

char * fastq_get_sequence(fastx_handle h)
{
  return h->sequence_buffer.data;
}

void fastq_print_header(FILE * fp, char * header)
{
  fprintf(fp, "@%s\n", header);
}

void fastq_print_sequence(FILE * fp, char * sequence)
{
  fprintf(fp, "%s\n", sequence);
}

void fastq_print_quality(FILE * fp, char * quality)
{
  fprintf(fp, "+\n%s\n", quality);
}

void fastq_print(FILE * fp, char * header, char * sequence, char * quality)
{
  fastq_print_header(fp, header);
  fastq_print_sequence(fp, sequence);
  fastq_print_quality(fp, quality);
}

void fastq_print_with_ee(FILE * fp, char * header,
                         char * sequence, char * quality, double ee)
{
  fprintf(fp, "@%s;ee=%6.4lf\n", header, ee);
  fastq_print_sequence(fp, sequence);
  fastq_print_quality(fp, quality);
}

void fastq_print_relabel(FILE * fp,
                         char * seq,
                         int len,
                         char * header,
                         int header_len,
                         char * quality,
                         int abundance,
                         int ordinal)
{
  fprintf(fp, "@");
  if (opt_relabel || opt_relabel_sha1 || opt_relabel_md5)
    {
      if (opt_relabel_sha1)
        fprint_seq_digest_sha1(fp, seq, len);
      else if (opt_relabel_md5)
        fprint_seq_digest_md5(fp, seq, len);
      else
        fprintf(fp, "%s%d", opt_relabel, ordinal);

      if (opt_sizeout)
        fprintf(fp, ";size=%u;", abundance);

      if (opt_relabel_keep)
        fprintf(fp, " %s", header);
    }
  else if (opt_sizeout)
    {
      abundance_fprint_header_with_size(global_abundance,
                                        fp,
                                        header,
                                        header_len,
                                        abundance);
    }
  else if (opt_xsize)
    {
      abundance_fprint_header_strip_size(global_abundance,
                                         fp,
                                         header,
                                         header_len);
    }
  else
    {
      fprintf(fp, "%s", header);
    }
  fprintf(fp, "\n");
  fastq_print_sequence(fp, seq);
  fastq_print_quality(fp, quality);
}

long fastq_get_abundance(fastx_handle h)
{
  return abundance_get(global_abundance, h->header_buffer.data);
}

void fastq_print_db(FILE * fp, unsigned long seqno)
{
  char * hdr = db_getheader(seqno);
  char * seq = db_getsequence(seqno);
  char * qual = db_getquality(seqno);

  fastq_print_header(fp, hdr);
  fastq_print_sequence(fp, seq);
  fastq_print_quality(fp, qual);
}
