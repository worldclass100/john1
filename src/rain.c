#include "os.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

#include "arch.h"
#include "int128.h"
#include "john.h"
#include "loader.h"
#include "cracker.h"
#include "options.h"
#include "config.h"
#include "logger.h"
#include "status.h"
#include "signals.h"
#include "external.h"
#include "recovery.h"
#include "mask.h"
#include "unicode.h"
#include "unicode_range.h"

#define MAX_CAND_LENGTH PLAINTEXT_BUFFER_SIZE
#define DEFAULT_MAX_LEN 16

#if JTR_HAVE_INT128
typedef uint128_t uint_big;
#define UINT_BIG_MAX UINT128_MAX
#else
typedef uint64_t uint_big;
#define UINT_BIG_MAX UINT64_MAX
#endif

int rain_cur_len;

static int rec_cur_len;
static char *charset;
static UTF32 rain[MAX_CAND_LENGTH+1];
static int charset_idx[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];//the first value should be req_maxlen-req_minlen
static int rec_charset_idx[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];
static int maxlength;
static int minlength;
static int state_restored;
static uint_big keyspace;
static uint_big subtotal;
static int strafe[MAX_CAND_LENGTH-1];
static int rec_strafe[MAX_CAND_LENGTH-1];
static uint_big drops[MAX_CAND_LENGTH-1];
static uint_big rec_drops[MAX_CAND_LENGTH-1];
static uint_big counter;
static uint_big rec_counter;
static int quick_conversion;
static int loop, rec_loop;//inner loop
static int set, rec_set;


static double get_progress(void)
{
	emms();

	if (!keyspace)
		return -1;
		
	if (rain_cur_len > maxlength)
		return 100;
	
	return (100.0 * counter) / (keyspace - subtotal);
}

static void fix_state(void)
{
	int i, j;
	
	rec_set = set;
	for (i = 0; i <= maxlength - minlength; ++i) {
		rec_strafe[i] = strafe[i];
		rec_drops[i] = drops[i];
		for(j = 0; j < maxlength; ++j)
			rec_charset_idx[i][j] = charset_idx[i][j];
	}
	rec_cur_len = rain_cur_len;
	rec_counter = counter;
	rec_loop = loop;
}


static void save_state(FILE *file)
{
	int i, j;
	
	fprintf(file, "%d\n", rec_set);
	for (i = 0; i <= maxlength - minlength; ++i) {
		fprintf(file, "%d\n ", rec_strafe[i]);	
		fprintf(file, "%llu\n ", rec_drops[i]);	
		for(j = 0; j < maxlength; ++j)
			fprintf(file, "%d\n", rec_charset_idx[i][j]);
	}
	fprintf(file, "%d\n", rec_cur_len);
	fprintf(file, "%llu\n", (unsigned long long) rec_counter);//changeme
	fprintf(file, "%d\n", rec_loop);
}

static int restore_state(FILE *file)
{
	int i, j, d;
	uint_big r;
	
	if(fscanf(file, "%d\n", &d) == 1)
		set = d;
	else return 1;

	for (i = 0; i <= maxlength - minlength; ++i) {
		if(fscanf(file, "%d\n ", &d) == 1)//all those bigint needs a fix in save and restore state
			strafe[i] = r;
		else return 1;
		if(fscanf(file, "%llu\n ", &r) == 1)//all those bigint needs a fix in save and restore state
			drops[i] = r;
		else return 1;
		
		for(j = 0; j < maxlength; ++j)
			if(fscanf(file, "%d\n", &d) == 1)
				charset_idx[i][j] = d;
			else return 1;
	}
	if(fscanf(file, "%d\n", &d) == 1)
		rain_cur_len = d;
	else return 1;

	if(fscanf(file, "%llu\n", (unsigned long long *) &r) == 1)
		counter = r;
	else return 1;
	
	if(fscanf(file, "%d\n", &d) == 1)
		loop = d;
	else return 1;
	
	state_restored = 1;

	return 0;
}




/* Parse \U+HHHH and \U+HHHHH notation to characters, in place. */
static void parse_unicode(char *string)
{
	static int warned;
	unsigned char *s = (unsigned char*)string;
	unsigned char *d = s;

	if (!string || !*string)
		return;

	while (*s)
		if (*s == '\\' && s[1] != 'U') {
			*d++ = *s++;
			*d++ = *s++;
		} else if (*s == '\\' && s[1] == 'U' && s[2] == '+' &&
		           atoi16[s[3]] != 0x7f && atoi16[s[4]] != 0x7f &&
		           atoi16[s[5]] != 0x7f && atoi16[s[6]] != 0x7f &&
		           atoi16[s[7]] != 0x7f) {
			UTF32 wc[2];
			UTF8 conv[8];
			char *c = (char*)conv;

			wc[0] = (atoi16[s[3]] << 16) + (atoi16[s[4]] << 12) +
				(atoi16[s[5]] << 8) + (atoi16[s[6]] << 4) + atoi16[s[7]];
			wc[1] = 0;
			if (!wc[0] && !warned++ && john_main_process)
				fprintf(stderr,
				        "Warning: \\U+00000 in mask terminates the string\n");
			if (wc[0] == '\\')
				*d++ = '\\';

			utf32_to_enc(conv, sizeof(conv), wc);

			while (*c)
				*d++ = *c++;
			s += 8;
		} else if (*s == '\\' && s[1] == 'U' && s[2] == '+' &&
		           atoi16[s[3]] != 0x7f && atoi16[s[4]] != 0x7f &&
		           atoi16[s[5]] != 0x7f && atoi16[s[6]] != 0x7f) {
			UTF32 wc[2];
			UTF8 conv[8];
			char *c = (char*)conv;

			wc[0] = (atoi16[s[3]] << 12) + (atoi16[s[4]] << 8) +
				(atoi16[s[5]] << 4) + atoi16[s[6]];
			wc[1] = 0;
			if (!wc[0] && !warned++ && john_main_process)
				fprintf(stderr,
				        "Warning: \\U+0000 in mask terminates the string\n");
			if (wc[0] == '\\')
				*d++ = '\\';

			utf32_to_enc(conv, sizeof(conv), wc);

			while (*c)
				*d++ = *c++;
			s += 7;
		} else
			*d++ = *s++;

	*d = 0;
}

static int submit(UTF32 *subset)
{
	UTF8 out[4 * MAX_CAND_LENGTH];
	int i;

	/* Set current word */
	if (quick_conversion) {
		/* Quick conversion (only ASCII or ISO-8859-1) */
		for (i = 0; i < minlength + loop; i++)
			out[i] = rain[i];
		out[i] = 0;
	} else if (options.target_enc == UTF_8) {
		/* Nearly as quick conversion, from UTF-8-32[tm] to UTF-8 */
		rain[minlength + loop] = 0;
		utf8_32_to_utf8(out, rain);
	} else {
		/* Slowest conversion, from real UTF-32 to sone legacy codepage */
		rain[minlength + loop] = 0;
		utf32_to_enc(out, sizeof(out), rain);
	}

	if (options.flags & FLG_MASK_CHK)
		return do_mask_crack((char*)out);
	else
		return crk_process_key((char*)out);
}

int do_rain_crack(struct db_main *db, char *req_charset)
{
	int i, j;
	int cp_max = 255;

	unsigned int charcount;
	int fmt_case = (db->format->params.flags & FMT_CASE);
	char *default_set;

	UTF32 *charset_utf32;

	maxlength = MIN(MAX_CAND_LENGTH, options.eff_maxlength);
	minlength = MAX(options.eff_minlength, 3);
		
	if (!options.req_maxlength)
		maxlength = MIN(maxlength, DEFAULT_MAX_LEN);
	if (!options.req_minlength)
		minlength = 3;

	default_set = (char*)cfg_get_param("Subsets", NULL, "DefaultCharset");
	if (!req_charset)
		req_charset = default_set;

	if (req_charset && *req_charset) {
		if (strlen(req_charset) == 1 && isdigit(req_charset[0])) {
			int cnum = atoi(req_charset);
			char pl[2] = { '0' + cnum, 0 };
			char *c = (char*)cfg_get_param("Subsets", NULL, pl);

			if (c)
				req_charset = c;
		}

		/* Parse \U+HHHH notation */
		parse_unicode(req_charset);
		charset = req_charset;
	} else if (fmt_case)
		charset = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
	else
		charset = "0123456789abcdefghijklmnopqrstuvwxyz !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

	charcount = strlen(charset);

	if (!strcasecmp(charset, "full-unicode")) {
		charset_utf32 = mem_alloc(0x22000 * sizeof(UTF32));
		charcount = full_unicode_charset(charset_utf32);
	}
	else if (options.input_enc == UTF_8) {
		if (!valid_utf8((UTF8*)charset)) {
			if (john_main_process)
				fprintf(stderr, "Error in Unicode conversion. "
				        "Ensure --input-encoding is correct\n");
			error();
		} else {
			int charsize = strlen8((UTF8*)charset) + 1;

			charset_utf32 = mem_alloc(charsize * sizeof(UTF32));
			utf8_to_utf32(charset_utf32, charsize * sizeof(UTF32),
			              (UTF8*)charset, charcount);
		}
	}
	else {
		charset_utf32 = mem_alloc((charcount + 1) * sizeof(UTF32));
		enc_to_utf32(charset_utf32, (charcount + 1) * sizeof(UTF32),
		             (UTF8*)charset, charcount);
	}

	//Performance step: Use UTF-32-8 when applicable
	if (options.target_enc == UTF_8)
		utf32_to_utf8_32(charset_utf32);

	charcount = strlen32(charset_utf32);
	
	if(charcount % 2 == 0 || charcount < 3) {
	    if( john_main_process )
	        fprintf(stderr, "Only character sets of odd lengths are supported and they must contain at least 3 symbols.\n");
	    error();
	}
	
	
	counter = 0;
	subtotal = 0;
	int loop2 = 0;
	
	status_init(get_progress, 0);
	rec_restore_mode(restore_state);
	rec_init(db, save_state);
	
	if(john_main_process) {
		log_event("Proceeding with \"rain\" mode");
		log_event("- Charset: %s size %d", req_charset ? req_charset : charset,
		          charcount);
		log_event("- Lengths: %d-%d, max",
		          MAX(options.eff_minlength, 1), maxlength);
		if(rec_restored) {
			fprintf(stderr, "Proceeding with \"rain\"%s%s",
			        req_charset ? ": " : "",
			        req_charset ? req_charset : "");
			if (options.flags & FLG_MASK_CHK)
				fprintf(stderr, ", hybrid mask:%s", options.mask ?
				        options.mask : options.eff_mask);
			if (options.rule_stack)
				fprintf(stderr, ", rules-stack:%s", options.rule_stack);
			if (options.req_minlength >= 0 || options.req_maxlength)
				fprintf(stderr, ", lengths: %d-%d",
				        options.eff_minlength + mask_add_len,
				        options.eff_maxlength + mask_add_len);
			fprintf(stderr, "\n");
		}
	}
		
	if(!state_restored)
	{
		rain_cur_len = minlength;
		
		for(i=0; i<= maxlength - minlength; i++) {
			strafe[i] = 0;
			drops[i] = 0;
			for (j = 0; j < maxlength; j++)
				charset_idx[i][j] = 0;
		}
	}
	
	keyspace = (uint_big) pow(charcount, rain_cur_len);
	if(rain_cur_len > minlength)
	subtotal = (uint_big) pow((double) charcount, (double) rain_cur_len-1);

	crk_init(db, fix_state, NULL);
	
	while((loop2 = rain_cur_len - minlength) <= maxlength - minlength) {
		if(event_abort) break;
		if(!state_restored)
			loop = loop2;
		/* Iterate over all lengths */
		while (loop <= maxlength - minlength) {
			if(event_abort) break;
			int skip = 0;
			
			if (state_restored)
				state_restored = 0;
			else
				set++;

			if (options.node_count) {
				int for_node = set % options.node_count + 1;
				skip = for_node < options.node_min ||
					for_node > options.node_max;
			}

			int mpl = minlength + loop;
            
			if(!skip) {
				quick_conversion = 1;
				for(i=0; i<mpl; ++i) {
					if( (rain[i] = charset_utf32[(charset_idx[loop][(strafe[loop]+i) % mpl] + drops[loop]) % charcount]) > cp_max) {
						quick_conversion = 0;
					}
				}
				submit(rain);
			}
			
			drops[loop]+=4;//odd length is enforced
	        if( drops[loop] % (charcount-2) < 2 ) drops[loop] = 0;
			
            int pos = mpl - 1;
			
			while(pos >= 0 && ++charset_idx[loop][pos] >= charcount) {
			    charset_idx[loop][pos] = 0;
			    --pos;
		    }
		    
	        if( mpl%2 )
		        strafe[loop] += 4;
			else
		        strafe[loop] += 2;
			
	        if( strafe[loop] % (mpl-2) < 2 ) strafe[loop] = 0;
			
			if(pos < 0) {
				counter = 0;
				++rain_cur_len;	
				keyspace = (uint_big) pow((double) charcount, (double) rain_cur_len);
				subtotal = (uint_big) pow((double) charcount, (double) rain_cur_len-1);
				if (cfg_get_bool("Subsets", NULL, "LengthIterStatus", 1))
					event_pending = event_status = 1;
			}
			++loop;		
			++counter;
		}
	}
	crk_done();
	rec_done(event_abort);
	MEM_FREE(charset_utf32);
	return 0;
}

