#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "m_pd.h"

#define MAX_VOICES 8
#define VERYLARGENUMBER 10000
#define MODULUS 12
#define HALFMODULUS 6
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
    int root_interval;                      // Root pitch class (0-11)
    int chord_structure[MAX_VOICES];        // Intervals from root
    int chord_structure_size;
    int chord_intervals[MAX_VOICES];        // Computed target pitch classes
    int chord_size;
    int feedback_enabled;
    int debug_enabled;
    int last_vl_cost;  // Store the cost of the last voice leading
} t_voice_leading;

typedef struct vl_path_t {
    int startPC;
    int path;
} vl_path_t;

typedef struct vl_result_t {
    int size;
    int num_paths;
    int path[MAX_VOICES];
    int startPCs[MAX_VOICES];  // Store which PCs these paths start from
} vl_result_t;

// Comparison function for qsort (sorts by size)
static int compare_vl_results(const void *a, const void *b) {
    vl_result_t *resultA = (vl_result_t *) a;
    vl_result_t *resultB = (vl_result_t *) b;
    return resultA->size - resultB->size;
}

// Helper function: comparison for sorting integers
static int compare_ints(const void *a, const void *b) {
    return (*(int *) a - *(int *) b);
}

// FIX: This function now properly rotates and finds the best voice leading
static vl_result_t bijective_vl(t_voice_leading *x, int *firstPCs, int *secondPCs, int length, bool sort) {
    static vl_result_t fullList[MAX_PERMUTATIONS];
    int fullList_count = 0;
    
    vl_result_t currentBest;
    currentBest.size = VERYLARGENUMBER;
    currentBest.num_paths = length;

    // Make a working copy of secondPCs that we can rotate
    int workingPCs[MAX_VOICES];
    memcpy(workingPCs, secondPCs, length * sizeof(int));

    if (x->debug_enabled) {
        post("DEBUG: bijective_vl - length: %d", length);
        post("DEBUG: firstPCs: [%d %d %d %d]", 
             firstPCs[0], firstPCs[1], firstPCs[2], firstPCs[3]);
        post("DEBUG: secondPCs: [%d %d %d %d]", 
             secondPCs[0], secondPCs[1], secondPCs[2], secondPCs[3]);
    }

    // Try all rotations
    for (int rotation = 0; rotation < length; rotation++) {
        // FIX: Reset newSize for each rotation
        int newSize = 0;
        vl_result_t newResult;
        newResult.num_paths = length;

        // Calculate paths for this specific permutation
        for (int voiceCounter = 0; voiceCounter < length; voiceCounter++) {
            int voice_path = (workingPCs[voiceCounter] - firstPCs[voiceCounter]) % MODULUS;
            
            // FIX: Handle negative modulo correctly
            if (voice_path < 0) voice_path += MODULUS;
            if (voice_path > HALFMODULUS) {
                voice_path -= MODULUS;
            }

            newResult.path[voiceCounter] = voice_path;
            newResult.startPCs[voiceCounter] = firstPCs[voiceCounter];
            newSize += abs(voice_path);
        }
        
        newResult.size = newSize;

        if (x->debug_enabled && rotation < 3) {  // Only log first 3 rotations
            post("DEBUG: Rotation %d - cost: %d, paths: [%d %d %d %d]",
                 rotation, newSize,
                 newResult.path[0], newResult.path[1], 
                 newResult.path[2], newResult.path[3]);
        }

        // Store result if we have space
        if (fullList_count < MAX_PERMUTATIONS) {
            fullList[fullList_count] = newResult;
            fullList_count++;
        }

        // Update best if this is better
        if (newSize < currentBest.size) {
            currentBest = newResult;
        }

        // FIX: Rotate the working copy for next iteration
        if (rotation < length - 1) {  // Don't rotate on last iteration
            int temp = workingPCs[length - 1];
            for (int j = length - 1; j > 0; j--) {
                workingPCs[j] = workingPCs[j - 1];
            }
            workingPCs[0] = temp;
        }
    }

    // Sort results if requested
    if (sort && fullList_count > 0) {
        qsort(fullList, fullList_count, sizeof(vl_result_t), compare_vl_results);
        currentBest = fullList[0];  // Take the best after sorting
    }

    if (x->debug_enabled) {
        post("DEBUG: Best voice leading cost: %d", currentBest.size);
    }

    return currentBest;
}

// FIX: Simplified signature and fixed logic
static void voicelead(t_voice_leading *x,
                      int *inPitches, int inPitches_size,
                      int *targetPCs, int targetPCs_size,
                      int *output, int *output_size) {
    
    // FIX: Compare values, not pointers
    if (inPitches_size != targetPCs_size) {
        pd_error(x, "voice_leading: voice count mismatch (current: %d, target: %d)",
                 inPitches_size, targetPCs_size);
        *output_size = 0;
        return;
    }

    // Convert input pitches to PCs and sort them
    int inPCs[MAX_VOICES];
    for (int i = 0; i < inPitches_size; i++) {
        inPCs[i] = inPitches[i] % MODULUS;
        if (inPCs[i] < 0) inPCs[i] += MODULUS;
    }
    qsort(inPCs, inPitches_size, sizeof(int), compare_ints);

    // Sort target PCs
    int sortedTargetPCs[MAX_VOICES];
    for (int i = 0; i < targetPCs_size; i++) {
        sortedTargetPCs[i] = targetPCs[i] % MODULUS;
        if (sortedTargetPCs[i] < 0) sortedTargetPCs[i] += MODULUS;
    }
    qsort(sortedTargetPCs, targetPCs_size, sizeof(int), compare_ints);

    // Find the best bijective voice leading
    vl_result_t best = bijective_vl(x, inPCs, sortedTargetPCs, inPitches_size, false);
    
    // Store the cost
    x->last_vl_cost = best.size;

    // Match each input pitch to its corresponding path
    bool path_used[MAX_VOICES] = {false};
    *output_size = 0;

    for (int i = 0; i < inPitches_size; i++) {
        int inPitch = inPitches[i];
        int inPC = inPitch % MODULUS;
        if (inPC < 0) inPC += MODULUS;

        // Find matching path that hasn't been used yet
        for (int j = 0; j < best.num_paths; j++) {
            if (!path_used[j] && inPC == best.startPCs[j]) {
                output[*output_size] = inPitch + best.path[j];
                (*output_size)++;
                path_used[j] = true;
                break;
            }
        }
    }

    if (*output_size != inPitches_size) {
        pd_error(x, "voice_leading: path assignment error (got %d, expected %d)",
                 *output_size, inPitches_size);
    }
}

// Main calculation
static void voice_leading_calculate(t_voice_leading *x) {
    if (x->current_size == 0 || x->chord_size == 0) {
        post("voice_leading: missing chord data (current: %d, chord: %d)",
             x->current_size, x->chord_size);
        return;
    }

    if (x->debug_enabled) {
        post("\nDEBUG: ===== Starting Voice Leading Calculation =====");
        post("DEBUG: Current chord: [%d %d %d %d]",
             x->current_chord[0], x->current_chord[1],
             x->current_chord[2], x->current_chord[3]);
        post("DEBUG: Target intervals: [%d %d %d %d]",
             x->chord_intervals[0], x->chord_intervals[1],
             x->chord_intervals[2], x->chord_intervals[3]);
    }

    int output_chord[MAX_VOICES];
    int output_chord_size = 0;
    
    voicelead(x,
              x->current_chord, x->current_size,
              x->chord_intervals, x->chord_size,
              output_chord, &output_chord_size);

    if (x->debug_enabled) {
        post("DEBUG: Output chord: [%d %d %d %d]",
             output_chord[0], output_chord[1],
             output_chord[2], output_chord[3]);
        post("DEBUG: Voice leading cost: %d", x->last_vl_cost);
    }

    // Output results
    t_atom out_list[MAX_VOICES];
    for (int i = 0; i < output_chord_size; i++) {
        SETFLOAT(&out_list[i], output_chord[i]);
    }
    
    outlet_float(x->x_out_cost, (t_float)x->last_vl_cost);
    outlet_list(x->x_out_chord, &s_list, output_chord_size, out_list);
    outlet_float(x->x_out_bass, (t_float)output_chord[0]);

    // Update feedback if enabled
    if (x->feedback_enabled) {
        memcpy(x->current_chord, output_chord, output_chord_size * sizeof(int));
        x->current_size = output_chord_size;
        
        if (x->debug_enabled) {
            post("DEBUG: Feedback enabled - updated current chord");
        }
    }
}

// Set current chord (COLD inlet)
static void voice_leading_current(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many voices (max %d)", MAX_VOICES);
        return;
    }
    
    x->current_size = argc;
    for (int i = 0; i < argc; i++) {
        x->current_chord[i] = (int)atom_getfloat(&argv[i]);
    }

    if (x->debug_enabled) {
        post("voice_leading: current chord set to [%d %d %d %d]",
             argc > 0 ? x->current_chord[0] : 0,
             argc > 1 ? x->current_chord[1] : 0,
             argc > 2 ? x->current_chord[2] : 0,
             argc > 3 ? x->current_chord[3] : 0);
    }
}

// Set root interval (COLD inlet)
static void voice_leading_root(t_voice_leading *x, t_floatarg f) {
    int root = (int)f % 12;
    if (root < 0) root += 12;
    x->root_interval = root;

    if (x->debug_enabled) {
        post("voice_leading: root set to %d", x->root_interval);
    }
}

// NEW: Set chord structure as intervals from root (HOT inlet)
static void voice_leading_chord(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many chord intervals (max %d)", MAX_VOICES);
        return;
    }

    // Store the chord structure
    x->chord_structure_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_structure[i] = (int)atom_getfloat(&argv[i]);
    }

    // Compute actual target pitch classes by adding root
    x->chord_size = argc;
    for (int i = 0; i < argc; i++) {
        int target_pc = (x->root_interval + x->chord_structure[i]) % 12;
        if (target_pc < 0) target_pc += 12;
        x->chord_intervals[i] = target_pc;
    }

    if (x->debug_enabled) {
        post("voice_leading: chord structure [%d %d %d %d] + root %d",
             argc > 0 ? x->chord_structure[0] : 0,
             argc > 1 ? x->chord_structure[1] : 0,
             argc > 2 ? x->chord_structure[2] : 0,
             argc > 3 ? x->chord_structure[3] : 0,
             x->root_interval);
        post("voice_leading:   = target PCs [%d %d %d %d]",
             argc > 0 ? x->chord_intervals[0] : 0,
             argc > 1 ? x->chord_intervals[1] : 0,
             argc > 2 ? x->chord_intervals[2] : 0,
             argc > 3 ? x->chord_intervals[3] : 0);
    }

    // HOT inlet - calculate immediately if we have current chord
    if (x->current_size > 0) {
        voice_leading_calculate(x);
    } else {
        pd_error(x, "voice_leading: no current chord set");
    }
}

// Set target chord intervals (HOT inlet - triggers calculation)
static void voice_leading_target(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many chord intervals (max %d)", MAX_VOICES);
        return;
    }

    x->chord_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_intervals[i] = (int)atom_getfloat(&argv[i]);
    }

    if (x->debug_enabled) {
        post("voice_leading: target set to [%d %d %d %d]",
             argc > 0 ? x->chord_intervals[0] : 0,
             argc > 1 ? x->chord_intervals[1] : 0,
             argc > 2 ? x->chord_intervals[2] : 0,
             argc > 3 ? x->chord_intervals[3] : 0);
    }

    // HOT inlet - calculate immediately if we have current chord
    if (x->current_size > 0) {
        voice_leading_calculate(x);
    } else {
        pd_error(x, "voice_leading: no current chord set");
    }
}

// Toggle feedback
static void voice_leading_feedback(t_voice_leading *x, t_floatarg f) {
    x->feedback_enabled = (f != 0);
    post("voice_leading: feedback %s", x->feedback_enabled ? "enabled" : "disabled");
}

// Toggle debug
static void voice_leading_debug(t_voice_leading *x, t_floatarg f) {
    x->debug_enabled = (f != 0);
    post("voice_leading: debug %s", x->debug_enabled ? "enabled" : "disabled");
}

// Bang - recalculate
static void voice_leading_bang(t_voice_leading *x) {
    voice_leading_calculate(x);
}

// Constructor
static void *voice_leading_new(void) {
    t_voice_leading *x = (t_voice_leading *)pd_new(voice_leading_class);

    // Create outlets (right to left)
    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    x->x_out_cost = outlet_new(&x->x_obj, &s_float);
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_bass = outlet_new(&x->x_obj, &s_float);

    // Initialize
    x->current_size = 0;
    x->chord_size = 0;
    x->chord_structure_size = 0;
    x->root_interval = 0;
    x->feedback_enabled = 1;
    x->debug_enabled = 0;
    x->last_vl_cost = 0;

    memset(x->current_chord, 0, MAX_VOICES * sizeof(int));
    memset(x->chord_structure, 0, MAX_VOICES * sizeof(int));
    memset(x->chord_intervals, 0, MAX_VOICES * sizeof(int));

    post("voice_leading: initialized (permutation-based bijective voice leading)");
    post("  Two modes: 1) absolute PCs with 'target', 2) root+intervals with 'chord'");
    
    return (void *)x;
}

// Setup function
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
    class_addmethod(voice_leading_class, (t_method)voice_leading_chord,
                    gensym("chord"), A_GIMME, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_target,
                    gensym("target"), A_GIMME, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_feedback,
                    gensym("feedback"), A_FLOAT, 0);
    class_addmethod(voice_leading_class, (t_method)voice_leading_debug,
                    gensym("debug"), A_FLOAT, 0);
    class_addbang(voice_leading_class, voice_leading_bang);
    
    post("voice_leading external loaded");
    post("Usage: [voice_leading]");
    post("  'current <pitches>' - set current chord");
    post("  'target <pcs>' - set target as absolute pitch classes (HOT)");
    post("  'root <pc>' + 'chord <intervals>' - set target as root+intervals (HOT)");
    post("  'feedback <0|1>' - enable/disable feedback");
    post("  'debug <0|1>' - enable/disable debug output");
}
