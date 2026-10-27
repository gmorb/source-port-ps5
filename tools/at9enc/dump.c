#include <stdio.h>
#include "atrac9tab.h"
#define N(a) (int)(sizeof(a)/sizeof((a)[0]))
#define DUMPI(name) do{ printf("\"%s\":[", #name); for(int i=0;i<N(name);i++) printf("%s%d", i?",":"", (int)name[i]); printf("],\n"); }while(0)
#define DUMPF(name) do{ printf("\"%s\":[", #name); for(int i=0;i<N(name);i++) printf("%s%.9g", i?",":"", (double)name[i]); printf("],\n"); }while(0)
int main(void){
  printf("{\n");
  DUMPI(at9_tab_sri_frame_log2); DUMPI(at9_tab_band_q_unit_map); DUMPI(at9_q_unit_to_coeff_cnt);
  DUMPI(at9_q_unit_to_coeff_idx); DUMPI(at9_q_unit_to_codebookidx); DUMPI(at9_tab_sri_max_bands);
  DUMPI(at9_tab_samplerates); DUMPF(at9_quant_step_coarse); DUMPF(at9_quant_step_fine);
  DUMPF(at9_scalefactor_c); DUMPI(at9_tab_b_dist);
  /* sf signed tables */
  printf("\"sfb_b\":["); for(int i=0;i<N(at9_sfb_b_tab);i++) printf("%s[%d,%d]", i?",":"", at9_sfb_b_tab[i][0], at9_sfb_b_tab[i][1]); printf("],\n");
  printf("\"sf_signed\":["); for(int i=0;i<N(at9_huffman_sf_signed);i++){const HuffmanCodebook*h=&at9_huffman_sf_signed[i]; printf("%s[%d,%d,%d,%d]", i?",":"", h->size,h->value_cnt,h->value_cnt_pow,h->value_bits);} printf("],\n");
  printf("\"sfb_a\":["); for(int i=0;i<N(at9_sfb_a_tab);i++) printf("%s[%d,%d]", i?",":"", at9_sfb_a_tab[i][0], at9_sfb_a_tab[i][1]); printf("],\n");
  printf("\"sf_unsigned\":["); for(int i=0;i<N(at9_huffman_sf_unsigned);i++){const HuffmanCodebook*h=&at9_huffman_sf_unsigned[i]; printf("%s[%d,%d,%d,%d]", i?",":"", h->size,h->value_cnt,h->value_cnt_pow,h->value_bits);} printf("],\n");
  printf("\"sf_weights\":["); for(int i=0;i<N(at9_tab_sf_weights);i++){ printf("%s[", i?",":""); for(int j=0;j<32;j++) printf("%s%d", j?",":"", at9_tab_sf_weights[i][j]); printf("]"); } printf("],\n");
  printf("\"coeffs_tab\":["); for(int i=0;i<N(at9_coeffs_tab);i++) printf("%s[%d,%d]", i?",":"", at9_coeffs_tab[i][0], at9_coeffs_tab[i][1]); printf("],\n");
  printf("\"huff_coeffs\":[");
  for(int i=0;i<2;i++){ printf("%s[", i?",":""); for(int j=0;j<8;j++){ printf("%s[", j?",":""); for(int k=0;k<4;k++){const HuffmanCodebook*h=&at9_huffman_coeffs[i][j][k]; printf("%s[%d,%d,%d,%d]", k?",":"", h->size,h->value_cnt,h->value_cnt_pow,h->value_bits);} printf("]"); } printf("]"); }
  printf("]\n}\n");
}
