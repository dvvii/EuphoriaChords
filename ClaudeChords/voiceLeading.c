#include "m_pd.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>  

#define MAX_VOICES 8
#define _VERYLARGENUMBER 10000
#define _MODULUS 12
#define _HALFMODULUS (int)(0.5 + _MODULUS / 2)
#define MAX_PERMUTATIONS 24  // 4! permutations for 4 voices


static t_class *voice_leading_class;

typedef struct _voice_leading {
    t_object x_obj;
    t_outlet *x_out_bass;
    t_outlet *x_out_chord;
    t_outlet *x_out_cost;
    t_outlet *x_out_info;
    
    int current_chord[MAX_VOICES];
    int current_size;
    int root_interval;
    int chord_intervals[MAX_VOICES];
    int chord_size;
    int feedback_enabled;
    int debug_enabled;
    
    int bijective_vl_size;  // Store size from bijective_vl

} t_voice_leading;

typedef struct vl_path_t {
    int startPC;
    int path;
} vl_path_t;

typedef struct vl_result_t {
    int size;
    int num_paths;
    int path[MAX_VOICES];
} vl_result_t;
#include <stdlib.h>  // For qsort

// Comparison function for qsort (sorts by size - the x[1] in Python)
static int compare_vl_results(const void *a, const void *b) {
    vl_result_t *resultA = (vl_result_t *)a;
    vl_result_t *resultB = (vl_result_t *)b;
    return resultA->size - resultB->size;  // Sort by size (ascending)
}

static int bijective_vl(t_voice_leading * x, int * firstPCs, int * secondPCs, bool sort) {
    
    int length = MAX_VOICES;
    static vl_result_t fullList[MAX_PERMUTATIONS];
    static int fullList_count = 0;
    
    static vl_path_t currentBest[MAX_VOICES];
    static int currentBestSize = _VERYLARGENUMBER;
    
    for(int i = 0; i < length; i++) {
        int temp = secondPCs[0];
        for(int j = 0; j < length - 1; j++) {
            secondPCs[j] = secondPCs[j + 1];
        }
        secondPCs[length - 1] = temp;
        static int newSize = 0;
        static vl_path_t newPaths[MAX_VOICES];
        for(int i = 0; i < length; i++) {
            int voice_path = (secondPCs[i] - firstPCs[i]) % _MODULUS;
            if (voice_path > _HALFMODULUS) {
                voice_path -= _MODULUS;
            }
            newSize += abs(voice_path);
            
            if (fullList_count < MAX_PERMUTATIONS) {
                fullList[fullList_count].size = newSize;
                fullList[fullList_count].num_paths = length;
                for (int i = 0; i < length; i++) {
                    fullList[fullList_count].path[i] = newPaths[i].path;
                }
                fullList_count++;
            }
            
            if (newSize < currentBestSize) {
                currentBestSize = newSize;
                for (int k = 0; k < length; k++) {
                    currentBest[k] = newPaths[k];
                }
            }
            x->bijective_vl_size = currentBestSize;
            return currentBest;
            if (sort) {
                qsort(fullList, fullList_count, sizeof(vl_result_t), compare_vl_results);
            }
        }
    } 