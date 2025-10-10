#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "m_pd.h"

#define MAX_VOICES 8
#define VERYLARGENUMBER 10000
#define MODULUS 12
#define HALFMODULUS (int)(0.5 + MODULUS / 2)
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
    int last_vl_cost;  // Store the cost of the last voice leading

    int bijective_vl_size; // Store size from bijective_vl
} t_voice_leading;

typedef struct vl_path_t {
    int startPC;
    int path;
} vl_path_t;

typedef struct vl_result_t {
    int size;
    int num_paths;
    int path[MAX_VOICES];
    int startPCs[MAX_VOICES]; //  store starting PCs-- needed?
} vl_result_t;

// Comparison function for qsort (sorts by size)
static int compare_vl_results(const void *a, const void *b) {
    vl_result_t *resultA = (vl_result_t *) a;
    vl_result_t *resultB = (vl_result_t *) b;
    return resultA->size - resultB->size; // Sort by size (ascending)
}

// Helper function: comparison for sorting integers
static int compare_ints(const void *a, const void *b) {
    return (*(int *) a - *(int *) b);
}

// Helper function: get random number in range [0, max)
static int random_range(int max) {
    return rand() % max;
}

static void sort_pitch_classes(int *pitches, int size, int *sorted_pcs) {
    // Convert to pitch classes
    for (int i = 0; i < size; i++) {
        sorted_pcs[i] = pitches[i] % 12;
        if (sorted_pcs[i] < 0) sorted_pcs[i] += 12;
    }

    // Sort (bubble sort)
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            if (sorted_pcs[j] < sorted_pcs[i]) {
                int temp = sorted_pcs[i];
                sorted_pcs[i] = sorted_pcs[j];
                sorted_pcs[j] = temp;
            }
        }
    }
}

static vl_path_t *bijective_vl(t_voice_leading *x, int *firstPCs, int *secondPCs, bool sort) {
    int length = x->current_size;

    static vl_result_t fullList[MAX_PERMUTATIONS];
    static int fullList_count = 0;
    fullList_count = 0;

    static vl_path_t currentBest[MAX_VOICES];
    static int currentBestSize = VERYLARGENUMBER;
    currentBestSize = VERYLARGENUMBER;

    //rotate secondPCs
    for (int i = 0; i < length; i++) {
        int temp = secondPCs[length - 1];
        for (int j = length - 1; j > 0; j--) {
            secondPCs[j] = secondPCs[j - 1];
        }
        secondPCs[0] = temp;

        static int newSize = 0;
        static vl_path_t newPaths[MAX_VOICES];

        //calculate paths for this specific permutation
        for (int voiceCounter = 0; voiceCounter < length; voiceCounter++) {
            int voice_path = (secondPCs[voiceCounter] - firstPCs[voiceCounter]) % MODULUS;
            if (voice_path > HALFMODULUS) {
                voice_path -= MODULUS;
            }

            newPaths[voiceCounter].startPC = firstPCs[voiceCounter];
            newPaths[voiceCounter].path = voice_path;
            newSize += abs(voice_path);
        }

        if (fullList_count < MAX_PERMUTATIONS) {
            fullList[fullList_count].size = newSize;
            fullList[fullList_count].num_paths = length;
            for (int pathIndex = 0; pathIndex < length; pathIndex++) {
                fullList[fullList_count].path[pathIndex] = newPaths[pathIndex].path;
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

        if (sort) {
            qsort(fullList, fullList_count, sizeof(vl_result_t), compare_vl_results);
            }
        }
    return currentBest; // return pointer to array
}

static void voicelead(t_voice_leading *x,
                            int *inPitches, int *inPitches_size,
                            int *targetPCs, int *targetPCs_size,
                            int topN, int *output, int *output_size) {
    if (inPitches_size != targetPCs_size) {
        *output_size = 0;
         //add PD error log
    }

    // convert input pitches to PCs and sort them
    int inPCs[MAX_VOICES];
    for (int i = 0; i < *inPitches_size; i++) {
        inPCs[i] = inPitches[i] % MODULUS;
    }
    qsort(inPCs, *inPitches_size, sizeof(int), compare_ints);

    //sort target PCs
    int sortedTargetPCs[MAX_VOICES];
    for (int i = 0; i < *targetPCs_size; i++) {
        sortedTargetPCs[i] = targetPCs[i];
        if (sortedTargetPCs[i] < 0) sortedTargetPCs[i] += MODULUS;
    }
    qsort(sortedTargetPCs, *targetPCs_size, sizeof(int), compare_ints);

    // Find the possible bijective voice leadings
    bool sort = (topN != 1);
    vl_path_t *paths = bijective_vl(x, inPCs, sortedTargetPCs, sort);
    int paths_size = *inPitches_size;

    //not using the random topN assignation

    vl_path_t tempPaths[MAX_VOICES];
    bool path_used[MAX_VOICES];
    for (int i = 0; i < paths_size; i++) {
        tempPaths[i] = paths[i];
        path_used[i] = false;
    }

    // Match each input pitch to its corresponding path
    *output_size = 0;
    for (int i = 0; i < *inPitches_size; i++) {
        int inPitch = inPitches[i];
        int inPC = inPitch % MODULUS;
        if (inPC < 0) inPC += MODULUS;

        // Find matching path that hasn't been used yet
        for (int j = 0; j < paths_size; j++) {
            if (!path_used[j] && inPC == tempPaths[j].startPC) {
                output[*output_size] = inPitch + tempPaths[j].path;
                (*output_size)++;
                path_used[j] = true;
                break;
            }
        }
    }
}

//Main calculation
static void voice_leading_calculate(t_voice_leading *x) {
    if (x->current_size == 0 || x->chord_size == 0) {
        post("voice_leading: missing chord data");
        return;
    }
  int output_chord[MAX_VOICES];
  int output_chord_size = 0;
  voicelead(x,
        x->current_chord,
        &x->current_size,
        x->chord_intervals,
        &x->chord_size,
        1,
        output_chord,
        &output_chord_size);
    if (x->debug_enabled) {
            post("DEBUG: Final voice leading solution:");
            for (int i = 0; i < output_chord_size; i++) {

            post("DEBUG: Voice %d: %d", i, output_chord[i]);
        }
    }
        for (int i = x->current_size; i < x->chord_size; i++) {

        }
        t_atom out_list[MAX_VOICES];
        for (int i = 0; i < output_chord_size; i++) {
            SETFLOAT(&out_list[i], output_chord[i]);
        }
    outlet_list(x->x_out_chord, &s_list, output_chord_size, out_list);
    outlet_float(x->x_out_bass, (t_float)output_chord[0]);

    if (x->feedback_enabled) {
        for (int i = 0; i < output_chord_size; i++) {
            x->current_chord[i] = output_chord[i];
        }
        x->current_size = output_chord_size;
    }
}

//set root
static void voice_leading_root(t_voice_leading *x, t_floatarg f) {
    int root = (int)f % 12;
    if (root < 0) root += 12;
    x->root_interval = root;
}

//Set current chord (COLD)
static void voice_leading_current(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many voices (max %d)", MAX_VOICES);
        return;
    }
    x->current_size = argc;
    for (int i = 0; i <argc; i++ ) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }

    if (x->debug_enabled) {
        post("voice_leading: current chord set to [%d %d %d %d]",
            x->current_chord[0], x->current_chord[1],
            x->current_chord[2], x->current_chord[3]);
    }
}

//set root interval (COLD)
static void _voice_leading(t_voice_leading *x, t_floatarg f) {
    int root = (int)f % 12;
    if (root < 0) root += 12;
    x->root_interval = root;

    if (x->debug_enabled) {
        post("voice_leading: root set to %d", x->root_interval);
    }
}

// Set chord intervals (HOT)
static void voice_leading_target(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many chord intervals (max %d)", MAX_VOICES);
        return;
    }

    x->chord_size = argc;
    for (int i =0; i < argc; i++) {
        x->chord_intervals[i] = (int)atom_getfloat(&argv[i]);
    }

    if (x->debug_enabled) {
        post("voice_leading: chord set to [%d %d %d %d]",
            x->chord_intervals[0], x->chord_intervals[1],
            x->chord_intervals[2], x->chord_intervals[3]);
    }

    if (x->current_size > 0) {
        voice_leading_calculate(x);
    } else {
        pd_error(x, "voice_leading: no current chord set");
    }
}

//toggle feedback
static void voice_leading_feedback(t_voice_leading *x, t_floatarg f) {
    x->feedback_enabled = (f != 0);
    post("voice_leading: feedback %s", x->feedback_enabled ? "enabled" : "disabled");
}

//toggle debug
static void voice_leading_debug(t_voice_leading *x, t_floatarg f) {
    x->debug_enabled = (f != 0);
    post("voice_leading: debug %s", x->debug_enabled ? "enabled" : "disabled");
}

//bang
static void voice_leading_bang(t_voice_leading *x) {
    voice_leading_calculate(x);
}

// Constructor

static void *voice_leading_new(void) {
    t_voice_leading *x = (t_voice_leading *)pd_new(voice_leading_class);

    x->x_out_bass = outlet_new(&x->x_obj, &s_float);
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);

    x->current_size = 0;
    x->chord_size = 0;
    x->root_interval = 0;
    x->feedback_enabled = 1;
    x->debug_enabled = 0;

    return (void *)x;
}



void voice_leading_setup(void) {
    voice_leading_class = class_new(gensym("voice_leading"),
                            (t_newmethod)voice_leading_new,
                            0,
                            sizeof(t_voice_leading),
                            CLASS_DEFAULT,
                            0);

    class_addmethod(voice_leading_class, (t_method)voice_leading_current,
                        gensym("current"), A_GIMME, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_root,
                        gensym("root"), A_FLOAT, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_target,
                        gensym("chord"), A_GIMME, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_feedback,
                        gensym("feedback"), A_FLOAT, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_debug,
                        gensym("debug"), A_FLOAT, 0);
    class_addbang(voice_leading_class, voice_leading_bang);
}