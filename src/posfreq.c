#include "os.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

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

#include "posfreq.h"

#define MAX_CAND_LENGTH PLAINTEXT_BUFFER_SIZE
#define DEFAULT_MAX_LEN 16

#if JTR_HAVE_INT128
typedef uint128_t uint_big;
#define UINT_BIG_MAX UINT128_MAX
#else
typedef uint64_t uint_big;
#define UINT_BIG_MAX UINT64_MAX
#endif

static char word[MAX_CAND_LENGTH+1];

static int maxlength;
static int minlength;
static int state_restored;
static uint64_t total;
int inc2_cur_len;
static int set;
static int rec_set;
static int rec_cur_len;
static uint_big counter[MAX_CAND_LENGTH-1];
static uint_big rec_counter[MAX_CAND_LENGTH-1];
static int state[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];
static int rec_state[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];
static int cs[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];
static int rec_cs[MAX_CAND_LENGTH-1][MAX_CAND_LENGTH];
static int loop;
static int rec_loop;
static char ***chrsts;
static int divi[MAX_CAND_LENGTH];
static int state1[MAX_CAND_LENGTH-1][8];
static int state2[MAX_CAND_LENGTH-1][8];
static int J[MAX_CAND_LENGTH-1];
static uint_big counter1[MAX_CAND_LENGTH-1][8];//todo loop2
static uint_big counter2[MAX_CAND_LENGTH-1][8];//todo loop2
static int inc[MAX_CAND_LENGTH-1];

const char letters[8] = "thiearon";
const int chainFreqCount[8] = {4, 2, 5, 5, 5, 3, 5, 4};

static double get_progress(void)
{
	emms();
	
	if (!total)
		return -1;
		
	if (inc2_cur_len > maxlength)
		return 100;
	
	return (100.0 * counter[maxlength-inc2_cur_len]) / total;
}

static uint_big powi(uint32_t b, uint32_t p)
{
	uint_big res = 1;

	if (b == 0)
		return 0;

	while (p--) {
		uint_big temp = res * b;

		if (temp < res)
			return UINT_BIG_MAX;
		res = temp;
	}

	return res;
}

static void big2str(uint_big orig, char *str) {
	uint_big b = orig, total = 0;
	int c = 0;
	int x;
	
	do {
		str[c] = b%10 + '0';
		total += (b % 10) * powi(10, c);
		b /= 10;
		++c;
	} while(total < orig);
	
	char tmp[c/2];
	//revert the characters
	for(x=0; x<c; ++x) {
		if(x<c/2+c%2) {
			tmp[x] = str[x];
			str[x] = str[c-x-1];
		}
		else {
			str[x] = tmp[c-x-1];
		}
	}
	str[c] = '\0';	
}

static uint_big str2big(char *str) {
	int x;
	static uint_big num = 0;
	int c = 0;
	for(x=strlen(str)-1; x>=0; --x) {
		num += (str[x]-'0') * powi(10, c);
		c++;
	}
	return num;
}

static void fix_state(void)
{
	int i, j;
	rec_set = set;
	for(i=0; i<=maxlength-minlength; i++) {
        for(j=0; j<minlength+i; j++) {
    	    rec_cs[i][j] = cs[i][j];
    	    rec_state[i][j] = state[i][j];
    	}
        rec_counter[i] = counter[i];
    }
	rec_cur_len = inc2_cur_len;
	rec_loop = loop;
}

/*
static void save_state(FILE *file)
{
	int i, j;
	char str[41];
	memset(str, 0, 41);
    fprintf(file, "%d\n", rec_set);
	for(i=0; i<=maxlength-minlength; i++) {
	    for(j=0; j<minlength+i; j++) {
		    fprintf(file, "%d\n", rec_cs[i][j]);
		    int k;
		    for(k=0; k<divi[j]; k++)
		        fprintf(file, "%d\n", rec_state[i][k][j]);
		}
        big2str(rec_counter[i], str);
        fprintf(file, "%s\n", str);
        memset(str, 0, 41);
    }
    for(i=0; i<maxlength; i++) {
        for(j=0; j<divi[i]; j++)
            fprintf(file, "%s\n", chrsts[i][j]);
    }
    fprintf(file, "%d\n", rec_cur_len);
	fprintf(file, "%d\n", rec_loop);
}

static int restore_state(FILE *file)
{
	int i, j, d;
	char str[41];
	memset(str, 0, 41);
	if(fscanf(file, "%d\n", &d) == 1)
		set = d;
	else return 1;

    for(i=0; i<=maxlength-minlength; i++) {
        for(j=0; j<minlength+i; j++) {
            if(fscanf(file, "%d\n", &d) == 1)
                cs[i][j] = d;
            else return 1;
            int k;
            for(k=0; k<divi[j]; k++)
                if(fscanf(file, "%d\n", &d) == 1)
                    state[i][k][j] = d;
                else return 1;
        }
        if(fscanf(file, "%s\n", str) == 1) {
            counter[i] = str2big(str);
            memset(str, 0, 41);
        }   
        else return 1;
    }
    for(i=0; i<maxlength; i++) {
        for(j=0; j<divi[i]; j++) {
	        if(fscanf(file, "%s\n", str) == 1) {
                memset(chrsts[i][j], 0, strlen(chrsts[i][j]));
                strcpy(chrsts[i][j], str);
                memset(str, 0, 41);
            }
            else return 1;
	    }
	}
	if(fscanf(file, "%d\n", &d) == 1)
		posfreq_cur_len = d;
	else return 1;

	if(fscanf(file, "%d\n", &d) == 1)
		loop = d;
	else return 1;

	state_restored = 1;

	return 0;
}
*/
static int submit(char *word, int loop2)
{
	char out[MAX_CAND_LENGTH+1];
	int i;

	/* Set current word */
	for (i = 0; i < minlength + loop2; i++)
		out[i] = word[i];
	out[i] = 0;

	if (options.flags & FLG_MASK_CHK)
		return do_mask_crack(out);
	else
		return crk_process_key(out);
}

int do_inc2_crack(struct db_main *db)
{
	static int i, j;
	unsigned int charcount;
	srand(time(NULL));
	maxlength = MIN(MAX_CAND_LENGTH, options.eff_maxlength);
	minlength = MAX(options.eff_minlength, 1);

	if (!options.req_maxlength)
		maxlength = MIN(maxlength, DEFAULT_MAX_LEN);
	if (!options.req_minlength)
		minlength = 1;

	charcount = 26;
	char **freq = (char **) mem_alloc(maxlength * sizeof(char *));
	for(i=0; i<maxlength; i++) {
		freq[i] = mem_alloc(charcount+1);
		switch(i) {
		case 0:
		    //strcpy(freq[i], "TtAa4Oo0Ii1Ss5WwCcBbPpFfMmHhDdRrEe3NnLlGgUuVvYyKkXxJjQqZz26789\0");
		    strcpy(freq[i], "taoiswcbpfmhdrenlguvykxjqz\0");
		    break;
		case 1:
		    //strcpy(freq[i], "ho0e3a4ni1rfuts5lpcmdybvwgxkjqzHOEANIRFUTSLPCMDYBVWGXKJQZ26789\0");
		    strcpy(freq[i], "hoeanirfutslpcmdybvwgxkjqz\0");
		    break;
		case 2:
		    //strcpy(freq[i], "e3rta4ds5o0ni1lmcupvgwyfbhkxjqzERTADSONILMCUPVGWYFBHKXJQZ26789\0");
		    strcpy(freq[i], "ertadsonilmcupvgwyfbhkxjqz\0");
		    break;
		case 3:
		    //strcpy(freq[i], "e3ti1rnls5a4o0dhcmupgywkvfbxjqzETIRNLSAODHCMUPGYWKVFBXJQZ26789\0");
		    strcpy(freq[i], "etirnlsaodhcmupgywkvfbxjqz\0");
		    break;
		case 4:
		    //strcpy(freq[i], "e3ri1ts5nlo0a4udchgmypvkbfwxjqzERITSNLOAUDCHGMYPVKBFWXJQZ26789\0");
		    strcpy(freq[i], "eritsnloaudchgmypvkbfwxjqz\0");
		    break;
		case 5:
		    //strcpy(freq[i], "e3nrs5i1ta4dlco0gmyhuvpwfbkxjqzENRSITADLCOGMYHUVPWFBKXJQZ26789\0");
		    strcpy(freq[i], "enrsitadlcogmyhuvpwfbkxjqz\0");
		    break;
		case 6:
		    //strcpy(freq[i], "e3ts5i1na4rldo0gycmuhpfbvwkxjqzETSINARLDOGYCMUHPFBVWKXJQZ26789\0");
		    strcpy(freq[i], "etsinarldogycmuhpfbvwkxjqz\0");
		    break;
		default:
		    break;
		}
		if(i > 6) {
			switch(maxlength-i) {
			case 7:
				//strcpy(freq[i], "e3ra4ci1o0s5tpnlmuhfdgbwvxqzjkyERACIOSTPNLMUHFDGBWVXQZJKY26789\0");
				strcpy(freq[i], "eraciostpnlmuhfdgbwvxqzjky\0");
				break;
			case 6:
				//strcpy(freq[i], "e3rs5a4i1o0tclnpumfhgbdvwyxkjqzERSAIOTCLNPUMFHGBDVWYXKJQZ26789\0");
				strcpy(freq[i], "ersaiotclnpumfhgbdvwyxkjqz\0");
				break;
	    	case 5:
				//strcpy(freq[i], "e3ta4ri1o0s5cnluwpmhgdfbvykxjqzETARIOSCNLUWPMHGDFBVYKXJQZ26789\0");
				strcpy(freq[i], "etarioscnluwpmhgdfbvykxjqz\0");
				break;
			case 4:
				//strcpy(freq[i], "ti1a4rho0e3s5lwnmcufdbpgvykxjqzTIARHOESLWNMCUFDBPGVYKXJQZ26789\0");
				strcpy(freq[i], "tiarhoeslwnmcufdbpgvykxjqz\0");
				break;
			case 3:
				//strcpy(freq[i], "i1ta4e3o0hnruls5wcdfbmvgpykxjqzITAEOHNRULSWCDFBMVGPYKXJQZ26789\0");
				strcpy(freq[i], "itaeohnrulswcdfbmvgpykxjqz\0");
				break;
	    	case 2:
				//strcpy(freq[i], "e3no0ha4i1trls5cumgdbwvkxpfyjqzENOHAITRLSCUMGDBWVKXPFYJQZ26789\0");
				strcpy(freq[i], "enohaitrlscumgdbwvkxpfyjqz\0");
				break;
			case 1:
				//strcpy(freq[i], "e3s5dntyrfo0lga4hmwcpi1bkuvxjqzESDNTYRFOLGAHMWCPIBKUVXJQZ26789\0");
				strcpy(freq[i], "esdntyrfolgahmwcpibkuvxjqz\0");
				break;
			default:
		        //strcpy(freq[i], "e3ta4o0i1ns5rhldcumfpgwybvkxjqzETAOINSRHLDCUMFPGWYBVKXJQZ26789\0");
	        	strcpy(freq[i], "etaoinsrhldcumfpgwybvkxjqz\0");
	        	break;
	    	}
		}
	}
	char ***chainFreq = (char ***) mem_alloc((maxlength-1) * sizeof(char **));
	for(i=1; i<maxlength; i++) {
		chainFreq[i-1] = mem_alloc(8 * sizeof(char *));
		for(j=0; j<8; j++)
			chainFreq[i-1][j] = mem_alloc(chainFreqCount[j]+1);
		switch(i) {
		case 1:
		    //strcpy(freq[i], "ho0e3a4ni1rfuts5lpcmdybvwgxkjqzHOEANIRFUTSLPCMDYBVWGXKJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "hoei");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "ontsc");
		    strcpy(chainFreq[i-1][3], "anrsd");
		    strcpy(chainFreq[i-1][4], "nrtsl");
		    strcpy(chainFreq[i-1][5], "eai");
		    strcpy(chainFreq[i-1][6], "nrfum");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		case 2:
		    //strcpy(freq[i], "e3rta4ds5o0ni1lmcupvgwyfbhkxjqzERTADSONILMCUPVGWYFBHKXJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "eoih");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "tsonc");
		    strcpy(chainFreq[i-1][3], "radsn");
		    strcpy(chainFreq[i-1][4], "rtsnl");
		    strcpy(chainFreq[i-1][5], "eai");
		    strcpy(chainFreq[i-1][6], "rnmuf");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		case 3:
		    //strcpy(freq[i], "e3ti1rnls5a4o0dhcmupgywkvfbxjqzETIRNLSAODHCMUPGYWKVFBXJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "eioh");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "tnsoc");
		    strcpy(chainFreq[i-1][3], "rnsad");
		    strcpy(chainFreq[i-1][4], "trnls");
		    strcpy(chainFreq[i-1][5], "eia");
		    strcpy(chainFreq[i-1][6], "rnmuf");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		case 4:
		    //strcpy(freq[i], "e3ri1ts5nlo0a4udchgmypvkbfwxjqzERITSNLOAUDCHGMYPVKBFWXJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "eioh");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "tsnoc");
		    strcpy(chainFreq[i-1][3], "rsnad");
		    strcpy(chainFreq[i-1][4], "rtsnl");
		    strcpy(chainFreq[i-1][5], "eia");
		    strcpy(chainFreq[i-1][6], "rnumf");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		case 5:
		    //strcpy(freq[i], "e3nrs5i1ta4dlco0gmyhuvpwfbkxjqzENRSITADLCOGMYHUVPWFBKXJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "eioh");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "nstco");
		    strcpy(chainFreq[i-1][3], "nrsad");
		    strcpy(chainFreq[i-1][4], "nrstl");
		    strcpy(chainFreq[i-1][5], "eia");
		    strcpy(chainFreq[i-1][6], "nrmuf");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		case 6:
		    //strcpy(freq[i], "e3ts5i1na4rldo0gycmuhpfbvwkxjqzETSINARLDOGYCMUHPFBVWKXJQZ26789\0");
		    strcpy(chainFreq[i-1][0], "eioh");
		    strcpy(chainFreq[i-1][1], "ei");
		    strcpy(chainFreq[i-1][2], "tsnoc");
		    strcpy(chainFreq[i-1][3], "snard");
		    strcpy(chainFreq[i-1][4], "tsnrl");
		    strcpy(chainFreq[i-1][5], "eia");
		    strcpy(chainFreq[i-1][6], "nrmuf");
		    strcpy(chainFreq[i-1][7], "etdg");
		    break;
		default:
		    break;
		}
		if(i > 6) {
			switch(maxlength-i) {
			case 7:
				//strcpy(freq[i], "e3ra4ci1o0s5tpnlmuhfdgbwvxqzjkyERACIOSTPNLMUHFDGBWVXQZJKY26789\0");
				strcpy(chainFreq[i-1][0], "eioh");
				strcpy(chainFreq[i-1][1], "ei");
				strcpy(chainFreq[i-1][2], "costn");
				strcpy(chainFreq[i-1][3], "rasnd");
				strcpy(chainFreq[i-1][4], "rstnl");
				strcpy(chainFreq[i-1][5], "eai");
				strcpy(chainFreq[i-1][6], "rnmuf");
				strcpy(chainFreq[i-1][7], "etdg");
				break;
			case 6:
				//strcpy(freq[i], "e3rs5a4i1o0tclnpumfhgbdvwyxkjqzERSAIOTCLNPUMFHGBDVWYXKJQZ26789\0");
				strcpy(chainFreq[i-1][0], "eioh");
				strcpy(chainFreq[i-1][1], "ei");
				strcpy(chainFreq[i-1][2], "sotcn");
				strcpy(chainFreq[i-1][3], "rsand");
				strcpy(chainFreq[i-1][4], "rstln");
				strcpy(chainFreq[i-1][5], "eai");
				strcpy(chainFreq[i-1][6], "rnumf");
				strcpy(chainFreq[i-1][7], "etgd");
				break;
	    	case 5:
				//strcpy(freq[i], "e3ta4ri1o0s5cnluwpmhgdfbvykxjqzETARIOSCNLUWPMHGDFBVYKXJQZ26789\0");
				strcpy(chainFreq[i-1][0], "eioh");
				strcpy(chainFreq[i-1][1], "ei");
				strcpy(chainFreq[i-1][2], "toscn");
				strcpy(chainFreq[i-1][3], "arsnd");
				strcpy(chainFreq[i-1][4], "trsnl");
				strcpy(chainFreq[i-1][5], "eai");
				strcpy(chainFreq[i-1][6], "rnumf");
				strcpy(chainFreq[i-1][7], "etgd");
				break;
			case 4:
				//strcpy(freq[i], "ti1a4rho0e3s5lwnmcufdbpgvykxjqzTIARHOESLWNMCUFDBPGVYKXJQZ26789\0");
				strcpy(chainFreq[i-1][0], "ihoe");
				strcpy(chainFreq[i-1][1], "ie");
				strcpy(chainFreq[i-1][2], "tosnc");
				strcpy(chainFreq[i-1][3], "arsnd");
				strcpy(chainFreq[i-1][4], "trsln");
				strcpy(chainFreq[i-1][5], "iae");
				strcpy(chainFreq[i-1][6], "rnmuf");
				strcpy(chainFreq[i-1][7], "tedg");
				break;
			case 3:
				//strcpy(freq[i], "i1ta4e3o0hnruls5wcdfbmvgpykxjqzITAEOHNRULSWCDFBMVGPYKXJQZ26789\0");
				strcpy(chainFreq[i-1][0], "ieoh");
				strcpy(chainFreq[i-1][1], "ie");
				strcpy(chainFreq[i-1][2], "tonsc");
				strcpy(chainFreq[i-1][3], "anrsd");
				strcpy(chainFreq[i-1][4], "tnrls");
				strcpy(chainFreq[i-1][5], "iae");
				strcpy(chainFreq[i-1][6], "nrufm");
				strcpy(chainFreq[i-1][7], "tedg");
				break;
	    	case 2:
				//strcpy(freq[i], "e3no0ha4i1trls5cumgdbwvkxpfyjqzENOHAITRLSCUMGDBWVKXPFYJQZ26789\0");
				strcpy(chainFreq[i-1][0], "eohi");
				strcpy(chainFreq[i-1][1], "ei");
				strcpy(chainFreq[i-1][2], "notsc");
				strcpy(chainFreq[i-1][3], "narsd");
				strcpy(chainFreq[i-1][4], "ntrls");
				strcpy(chainFreq[i-1][5], "eai");
				strcpy(chainFreq[i-1][6], "nrumf");
				strcpy(chainFreq[i-1][7], "etgd");
				break;
			case 1:
				//strcpy(freq[i], "e3s5dntyrfo0lga4hmwcpi1bkuvxjqzESDNTYRFOLGAHMWCPIBKUVXJQZ26789\0");
				strcpy(chainFreq[i-1][0], "eohi");
				strcpy(chainFreq[i-1][1], "ei");
				strcpy(chainFreq[i-1][2], "sntoc");
				strcpy(chainFreq[i-1][3], "sdnra");
				strcpy(chainFreq[i-1][4], "sntrl");
				strcpy(chainFreq[i-1][5], "eai");
				strcpy(chainFreq[i-1][6], "nrfmu");
				strcpy(chainFreq[i-1][7], "edtg");
				break;
			default:
		        //strcpy(freq[i], "e3ta4o0i1ns5rhldcumfpgwybvkxjqzETAOINSRHLDCUMFPGWYBVKXJQZ26789\0");
	        	strcpy(chainFreq[i-1][0], "eoih");
	        	strcpy(chainFreq[i-1][1], "ei");
	        	strcpy(chainFreq[i-1][2], "tonsc");
	        	strcpy(chainFreq[i-1][3], "ansrd");
	        	strcpy(chainFreq[i-1][4], "tnsrl");
	        	strcpy(chainFreq[i-1][5], "eai");
	        	strcpy(chainFreq[i-1][6], "nrumf");
	        	strcpy(chainFreq[i-1][7], "etdg");
	        	break;
			}
		}
	}
	char ***counterChainFreq = (char ***) mem_alloc((maxlength-1) * sizeof(char **));
	for(i=1; i<maxlength; i++) {
		counterChainFreq[i-1] = mem_alloc(8 * sizeof(char *));
		for(j=0; j<8; j++)
			counterChainFreq[i-1][j] = mem_alloc(charcount-chainFreqCount[j]+1);

		switch(i) {
		case 1:
		    //strcpy(freq[i], "ho0e3a4ni1rfuts5lpcmdybvwgxkjqzHOEANIRFUTSLPCMDYBVWGXKJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "anrfutslpcmdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][1], "hoanrfutslpcmdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][2], "heairfulpmdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][3], "hoeifutlpcmybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][4], "hoeaifupcmdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][5], "honrfutslpcmdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][6], "hoeaitslpcdybvwgxkjqz\0");
		    strcpy(counterChainFreq[i-1][7], "hoanirfuslpcmybvwxkjqz\0");
		    break;
		case 2:
		    //strcpy(freq[i], "e3rta4ds5o0ni1lmcupvgwyfbhkxjqzERTADSONILMCUPVGWYFBHKXJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "rtadsnlmcupvgwyfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][1], "rtadsonlmcupvgwyfbhkxjqz\0");
		    strcpy(counterChainFreq[i-1][2], "eradilmupvgwyfbhkxjqz\0");
		    strcpy(counterChainFreq[i-1][3], "etoilmcupvgwyfbhkxjqz\0");
		    strcpy(counterChainFreq[i-1][4], "eadoimcupvgwyfbhkxjqz\0");
		    strcpy(counterChainFreq[i-1][5], "rtdsonlmcupvgwyfbhkxjqz\0");
		    strcpy(counterChainFreq[i-1][6], "etadsoilcpvgwybhkxjqz\0");
		    strcpy(counterChainFreq[i-1][7], "rasonilmcupvwyfbhkxjqz\0");
		    break;
		case 3:
		    //strcpy(freq[i], "e3ti1rnls5a4o0dhcmupgywkvfbxjqzETIRNLSAODHCMUPGYWKVFBXJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "trnlsadcmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][1], "trnlsaodhcmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][2], "eirladhmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][3], "etilohcmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][4], "eiaodhcmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][5], "trnlsodhcmupgywkvfbxjqz\0");
		    strcpy(counterChainFreq[i-1][6], "etilsaodhcpgywkvbxjqz\0");
		    strcpy(counterChainFreq[i-1][7], "irnlsaohcmupywkvfbxjqz\0");
		    break;
		case 4:
		    //strcpy(freq[i], "e3ri1ts5nlo0a4udchgmypvkbfwxjqzERITSNLOAUDCHGMYPVKBFWXJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "rtsnlaudcgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][1], "rtsnloaudchgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][2], "erilaudhgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][3], "eitlouchgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][4], "eioaudchgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][5], "rtsnloudchgmypvkbfwxjqz\0");
		    strcpy(counterChainFreq[i-1][6], "eitsloadchgypvkbwxjqz\0");
		    strcpy(counterChainFreq[i-1][7], "risnloauchmypvkbfwxjqz\0");
		    break;
		case 5:
		    //strcpy(freq[i], "e3nrs5i1ta4dlco0gmyhuvpwfbkxjqzENRSITADLCOGMYHUVPWFBKXJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "nrstadlcgmyuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][1], "nrstadlcogmyhuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][2], "eriadlgmyhuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][3], "eitlcogmyhuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][4], "eiadcogmyhuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][5], "nrstdlcogmyhuvpwfbkxjqz\0");
		    strcpy(counterChainFreq[i-1][6], "esitadlcogyhvpwbkxjqz\0");
		    strcpy(counterChainFreq[i-1][7], "nrsialcomyhuvpwfbkxjqz\0");
		    break;
		case 6:
		    //strcpy(freq[i], "e3ts5i1na4rldo0gycmuhpfbvwkxjqzETSINARLDOGYCMUHPFBVWKXJQZ26789\0");
		    strcpy(counterChainFreq[i-1][0], "tsnarldgycmupfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][1], "tsnarldogycmuhpfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][2], "eiarldgymuhpfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][3], "etilogycmuhpfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][4], "eiadogycmuhpfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][5], "tsnrldogycmuhpfbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][6], "etsialdogychpbvwkxjqz\0");
		    strcpy(counterChainFreq[i-1][7], "sinarloycmuhpfbvwkxjqz\0");
		    break;
		default:
		    break;
		}
		if(i > 6) {
			switch(maxlength-i) {
			case 7:
				//strcpy(freq[i], "e3ra4ci1o0s5tpnlmuhfdgbwvxqzjkyERACIOSTPNLMUHFDGBWVXQZJKY26789\0");
				strcpy(counterChainFreq[i-1][0], "racstpnlmufdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][1], "racostpnlmuhfdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][2], "eraiplmuhfdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][3], "eciotplmuhfgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][4], "eaciopmuhfdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][5], "rcostpnlmuhfdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][6], "eaciostplhdgbwvxqzjky\0");
				strcpy(counterChainFreq[i-1][7], "raciospnlmuhfbwvxqzjky\0");
				break;
			case 6:
				//strcpy(freq[i], "e3rs5a4i1o0tclnpumfhgbdvwyxkjqzERSAIOTCLNPUMFHGBDVWYXKJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "rsatclnpumfgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][1], "rsaotclnpumfhgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][2], "erailpumfhgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][3], "eiotclpumfhgbvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][4], "eaiocpumfhgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][5], "rsotclnpumfhgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][6], "esaiotclphgbdvwyxkjqz\0");
				strcpy(counterChainFreq[i-1][7], "rsaioclnpumfhbvwyxkjqz\0");
				break;
	    	case 5:
				//strcpy(freq[i], "e3ta4ri1o0s5cnluwpmhgdfbvykxjqzETARIOSCNLUWPMHGDFBVYKXJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "tarscnluwpmgdfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][1], "taroscnluwpmhgdfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][2], "eariluwpmhgdfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][3], "etiocluwpmhgfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][4], "eaiocuwpmhgdfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][5], "troscnluwpmhgdfbvykxjqz\0");
				strcpy(counterChainFreq[i-1][6], "etaiosclwphgdbvykxjqz\0");
				strcpy(counterChainFreq[i-1][7], "arioscnluwpmhfbvykxjqz\0");
				break;
			case 4:
				//strcpy(freq[i], "ti1a4rho0e3s5lwnmcufdbpgvykxjqzTIARHOESLWNMCUFDBPGVYKXJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "tarslwnmcufdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][1], "tarhoslwnmcufdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][2], "iarhelwmufdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][3], "tihoelwmcufbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][4], "iahoewmcufdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][5], "trhoslwnmcufdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][6], "tiahoeslwcdbpgvykxjqz\0");
				strcpy(counterChainFreq[i-1][7], "iarhoslwnmcufbpvykxjqz\0");
				break;
			case 3:
				//strcpy(freq[i], "i1ta4e3o0hnruls5wcdfbmvgpykxjqzITAEOHNRULSWCDFBMVGPYKXJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "tanrulswcdfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][1], "taohnrulswcdfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][2], "iaehrulwdfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][3], "iteohulwcfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][4], "iaeohuwcdfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][5], "tohnrulswcdfbmvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][6], "itaeohlswcdbvgpykxjqz\0");
				strcpy(counterChainFreq[i-1][7], "iaohnrulswcfbmvpykxjqz\0");
				break;
	    	case 2:
				//strcpy(freq[i], "e3no0ha4i1trls5cumgdbwvkxpfyjqzENOHAITRLSCUMGDBWVKXPFYJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "natrlscumgdbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][1], "nohatrlscumgdbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][2], "ehairlumgdbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][3], "eohitlcumgbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][4], "eohaicumgdbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][5], "nohtrlscumgdbwvkxpfyjqz\0");
				strcpy(counterChainFreq[i-1][6], "eohaitlscgdbwvkxpyjqz\0");
				strcpy(counterChainFreq[i-1][7], "nohairlscumbwvkxpfyjqz\0");
				break;
			case 1:
				//strcpy(freq[i], "e3s5dntyrfo0lga4hmwcpi1bkuvxjqzESDNTYRFOLGAHMWCPIBKUVXJQZ26789\0");
				strcpy(counterChainFreq[i-1][0], "sdntyrflgamwcpbkuvxjqz\0");
				strcpy(counterChainFreq[i-1][1], "sdntyrfolgahmwcpbkuvxjqz\0");
				strcpy(counterChainFreq[i-1][2], "edyrflgahmwpibkuvxjqz\0");
				strcpy(counterChainFreq[i-1][3], "etyfolghmwcpibkuvxjqz\0");
				strcpy(counterChainFreq[i-1][4], "edyfogahmwcpibkuvxjqz\0");
				strcpy(counterChainFreq[i-1][5], "sdntyrfolghmwcpbkuvxjqz\0");
				strcpy(counterChainFreq[i-1][6], "esdtyolgahwcpibkvxjqz\0");
				strcpy(counterChainFreq[i-1][7], "snyrfolahmwcpibkuvxjqz\0");
				break;
			default:
		        //strcpy(freq[i], "e3ta4o0i1ns5rhldcumfpgwybvkxjqzETAOINSRHLDCUMFPGWYBVKXJQZ26789\0");
	        	strcpy(counterChainFreq[i-1][0], "tansrldcumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][1], "taonsrhldcumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][2], "eairhldumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][3], "etoihlcumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][4], "eaoihdcumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][5], "tonsrhldcumfpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][6], "etaoishldcpgwybvkxjqz\0");
	        	strcpy(counterChainFreq[i-1][7], "aoinsrhlcumfpwybvkxjqz\0");
	        	break;
	    	}
		}
	}
	inc2_cur_len = minlength;
	for(i=0; i<maxlength; i++) {
		divi[i] = charcount/4;
        if(charcount % 4)
		    divi[i]++;
	}
	chrsts = (char ***) mem_alloc(sizeof(char **) * maxlength);
	for(i=0; i<maxlength; i++) {
		chrsts[i] = (char **) mem_alloc(sizeof(char *) * divi[i]);
		for(j=0; j<divi[i]; j++)
			chrsts[i][j] = (char *) mem_alloc(5);
	}
	status_init(get_progress, 0);
	//rec_restore_mode(restore_state);
	//rec_init(db, save_state);
	if(john_main_process) {
		log_event("Proceeding with \"posfreq\" mode");
		log_event("- Lengths: %d-%d, max",
		          MAX(options.eff_minlength, 1), maxlength);
		if(rec_restored) {
			fprintf(stderr, "Proceeding with \"posfreq\" mode");
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
    int x,y,z;
	crk_init(db, fix_state, NULL);
	if(!state_restored) {
		for(x=0; x<maxlength; x++) {
			for(y=0; y<divi[x]; y++) {
				int Z = 4;
				int min = 0;
				if(y == divi[x]-1) {
					Z = charcount % 4;
					min = Z;
				}
				for(z=0; z<Z; z++) {
					int again;
					chrsts[x][y][z] = freq[x][rand()%((y+1)*4-min)];
					again = 0;
					for(i=0; i<=y; i++) {
						int Z2 = 4;
						if(i == divi[x]-1)
							Z2 = charcount % 4;
						
						for(j=0; j<Z2; j++) {
							if(z == j && i == y)
								continue;
							if(chrsts[x][y][z] == chrsts[x][i][j]) {
								again = 1;
								break;
							}
						}
						if(again)
							break;
					}
					if(again) {
						z--;
						continue;
					}
					chrsts[x][y][z+1] = '\0';
				}
				printf("%s\n", chrsts[x][y]);
			}
		}	
		for(i = 0; i <= maxlength-minlength; i++) {    
			counter[i] = 0;
			for(j = 0; j < minlength+i; j++) {
				cs[i][j] = 0;
				state[i][j] = 0;
			}
		}
	}
	for(; loop <= maxlength-minlength; loop++) {
		if(event_abort)
			break;
		uint_big total = powi(charcount, minlength+loop);
		for(; counter[loop] < total; ) {
			if(event_abort)
				break;
			int loop2;
			for(loop2 = loop; loop2 <= maxlength-minlength; loop2++) {
				if(event_abort) break;
        		int mpl = minlength + loop2;
				int skip = 0;
				if (state_restored) state_restored = 0;
	        	else set++;
		        if(options.node_count) {
		        	int for_node = set % options.node_count + 1;
		        	skip = for_node < options.node_min || for_node > options.node_max;
		        }
		    	if(!skip) {
					word[0] = freq[0][state[loop2][0]];
		        	for(i=1; i<mpl; i++) {
		        	    for(j=0; j<8; j++) {
						    if(letters[j] == word[i-1]) {
                                inc[i-1] = 1;
                                J[i-1] = j;
						        if(state1[i-1][j] >= chainFreqCount[j])
						            inc[i-1] = 2;
						        break;
						    }
						    //will reach here if no chaining
						    else inc[i-1] = 0;
						}
						switch(inc[i-1]) {
						    case 0:
							    word[i] = freq[i][state[loop2][i]];
							    break;
						    case 1:
							    word[i] = chainFreq[i-1][J[i-1]][state1[i-1][J[i-1]]];
							    break;
						    case 2:
							    word[i] = counterChainFreq[i-1][J[i-1]][state2[i-1][J[i-1]]];		
							    break;		
						}
					}
					submit(word, loop2);
				}
				i = mpl-1;
				int bail = 0;
				while(i >= 0 && !bail) {
				    int a = 0;
					if(i > 0) {
					    for(j=0; j<8; j++) {
						    if(letters[j] == word[i-1]) {
						        inc[i-1] = 1;
						        J[i-1] = j;
						        if(state1[i-1][j] >= chainFreqCount[j])
						            inc[i-1] = 2;
						        break;
						    }
						    else inc[i-1] = 0;
						}
					    switch(inc[i-1]) {
            			    case 0:
                				if(++state[loop2][i] >= strlen(freq[i])) {
						            state[loop2][i] = 0;
						            i--;
					            }
					            else bail = 1;

                				break;
            			    case 1:
                	            if(++state1[i-1][J[i-1]] >= chainFreqCount[J[i-1]]) {
						            inc[i-1] = 2;
					                a = 1;
					            }
					            else bail = 1;

					            if(!a) break;

				            case 2:
					            if(state2[i-1][J[i-1]] >= charcount - chainFreqCount[J[i-1]]-1) {
						            state1[i-1][J[i-1]] = 0;
						            state2[i-1][J[i-1]] = 0;
						            inc[i-1] = 0;
						            i--;
					            }
					            else bail = 1;

                                if(a) a = 0;
                                else state2[i-1][J[i-1]]++;

					            break;
			            }
					}
					else {
                        if(++state[loop2][0] >= charcount) {
                            state[loop2][0] = 0;
							i--;
                            /*
						    int pos = mpl - 1;
							if(pos < 0) {
								int pos2 = mpl - 1;
								while(pos2 >= 0 && ++cs[loop2][pos2] >= divi[pos2]) {
									cs[loop2][pos2] = 0;
									pos2--;
								}
							}
							*/
						}
						else break;
					}
				}
				counter[loop2]++;
			}
		}
		inc2_cur_len++;
	}
	crk_done();
	rec_done(event_abort);
	return 0;
}

