#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "m_pd.h"

#define MAX_VOICES 8
#define MAX_MATRIX_SIZE 16  // For dynamic programming matrix
#define VERYLARGENUMBER 10000
#define MODULUS 12
#define HALFMODULUS 6

static t_class *voice_leading_class;

typedef struct _voice_leading {
    t_object x_obj;
    t_outlet *x_out_root;
    t_outlet *x_out_chord;
    t_outlet *x_out_info;

    int current_chord[MAX_VOICES];
    int current_size;
    int root_interval;
    int chord_structure[MAX_VOICES];
    int chord_structure_size;
    int chord_intervals[MAX_VOICES];
    int chord_size;
    int feedback_enabled;
    int debug_enabled;
    int last_vl_cost;
} t_voice_leading;

typedef struct {
    int source_note;
    int target_note;
} voice_pair_t;

// Helper: calculate PC distance (min of both directions)
static int pc_distance(int pc1, int pc2) {
    int forward = (pc2 - pc1 + MODULUS) % MODULUS;
    int backward = (pc1 - pc2 + MODULUS) % MODULUS;
    return (forward < backward) ? forward : backward;
}

// Helper: remove duplicates and sort
static int remove_duplicates_and_sort(int *input, int input_size, int *output) {
    if (input_size == 0) return 0;

    // Copy and sort
    int temp[MAX_VOICES];
    for (int i = 0; i < input_size; i++) {
        temp[i] = input[i];
    }

    // Bubble sort
    for (int i = 0; i < input_size - 1; i++) {
        for (int j = i + 1; j < input_size; j++) {
            if (temp[j] < temp[i]) {
                int swap = temp[i];
                temp[i] = temp[j];
                temp[j] = swap;
            }
        }
    }

    // Remove duplicates
    int out_size = 0;
    output[out_size++] = temp[0];
    for (int i = 1; i < input_size; i++) {
        if (temp[i] != temp[i-1]) {
            output[out_size++] = temp[i];
        }
    }

    return out_size;
}

// Build the dynamic programming matrix
static int build_matrix(t_voice_leading *x,
                        int *source, int source_size,
                        int *target, int target_size,
                        int matrix[MAX_MATRIX_SIZE][MAX_MATRIX_SIZE],
                        int output_matrix[MAX_MATRIX_SIZE][MAX_MATRIX_SIZE]) {

    if (x->debug_enabled) {
        post("DEBUG: Building matrix %dx%d", target_size, source_size);
    }

    // Initialize distance matrix
    for (int i = 0; i < target_size; i++) {
        for (int j = 0; j < source_size; j++) {
            matrix[i][j] = pc_distance(source[j], target[i]);
        }
    }

    // Initialize output matrix (copy distance matrix)
    for (int i = 0; i < target_size; i++) {
        for (int j = 0; j < source_size; j++) {
            output_matrix[i][j] = matrix[i][j];
        }
    }

    // Fill first row (cumulative)
    for (int j = 1; j < source_size; j++) {
        output_matrix[0][j] += output_matrix[0][j-1];
    }

    // Fill first column (cumulative)
    for (int i = 1; i < target_size; i++) {
        output_matrix[i][0] += output_matrix[i-1][0];
    }

    // Fill rest of matrix with minimum cumulative cost
    for (int i = 1; i < target_size; i++) {
        for (int j = 1; j < source_size; j++) {
            int from_left = output_matrix[i][j-1];
            int from_above = output_matrix[i-1][j];
            int from_diagonal = output_matrix[i-1][j-1];

            int min_cost = from_diagonal;
            if (from_left < min_cost) min_cost = from_left;
            if (from_above < min_cost) min_cost = from_above;

            output_matrix[i][j] += min_cost;
        }
    }

    // Return total cost (minus the last cell's own distance)
    int total_cost = output_matrix[target_size-1][source_size-1] -
                     matrix[target_size-1][source_size-1];

    if (x->debug_enabled) {
        post("DEBUG: Matrix total cost: %d", total_cost);
    }

    return total_cost;
}

// Backtrack through matrix to find voice leading
static int find_matrix_vl(t_voice_leading *x,
                          int *source, int source_size,
                          int *target, int target_size,
                          int output_matrix[MAX_MATRIX_SIZE][MAX_MATRIX_SIZE],
                          voice_pair_t *vl) {

    int i = target_size - 1;
    int j = source_size - 1;
    int vl_count = 0;

    // Add final pairing
    vl[vl_count].source_note = source[j];
    vl[vl_count].target_note = target[i];
    vl_count++;

    // Backtrack through matrix
    while (i > 0 || j > 0) {
        int new_i = i;
        int new_j = j;

        if (i > 0 && j > 0) {
            // Can move diagonally, left, or up - choose minimum
            int diagonal = output_matrix[i-1][j-1];
            int from_above = output_matrix[i-1][j];
            int from_left = output_matrix[i][j-1];

            int min_cost = diagonal;
            new_i = i - 1;
            new_j = j - 1;

            if (from_above < min_cost) {
                min_cost = from_above;
                new_i = i - 1;
                new_j = j;
            }

            if (from_left < min_cost) {
                min_cost = from_left;
                new_i = i;
                new_j = j - 1;
            }

            i = new_i;
            j = new_j;
        } else if (i > 0) {
            i = i - 1;
        } else if (j > 0) {
            j = j - 1;
        }

        vl[vl_count].source_note = source[j];
        vl[vl_count].target_note = target[i];
        vl_count++;
    }

    // Reverse the voice leading (we built it backwards)
    for (int k = 0; k < vl_count / 2; k++) {
        voice_pair_t temp = vl[k];
        vl[k] = vl[vl_count - 1 - k];
        vl[vl_count - 1 - k] = temp;
    }

    if (x->debug_enabled) {
        post("DEBUG: Found %d voice pairs", vl_count);
        for (int k = 0; k < vl_count; k++) {
            post("DEBUG:   [%d] %d -> %d", k, vl[k].source_note, vl[k].target_note);
        }
    }

    return vl_count;
}

// Nonbijective voice leading algorithm
static void nonbijective_vl(t_voice_leading *x,
                            int *source_pcs, int source_size,
                            int *target_pcs, int target_size,
                            voice_pair_t *best_vl, int *best_vl_size) {

    // Remove duplicates and sort both sets
    int unique_source[MAX_VOICES];
    int unique_target[MAX_VOICES];
    int unique_source_size = remove_duplicates_and_sort(source_pcs, source_size, unique_source);
    int unique_target_size = remove_duplicates_and_sort(target_pcs, target_size, unique_target);

    if (x->debug_enabled) {
        post("DEBUG: Unique source size: %d, unique target size: %d",
             unique_source_size, unique_target_size);
    }

    int best_cost = VERYLARGENUMBER;
    voice_pair_t temp_vl[MAX_MATRIX_SIZE];
    int matrix[MAX_MATRIX_SIZE][MAX_MATRIX_SIZE];
    int output_matrix[MAX_MATRIX_SIZE][MAX_MATRIX_SIZE];

    // Try all inversions of target
    for (int inversion = 0; inversion < unique_target_size; inversion++) {
        // Rotate target
        int rotated_target[MAX_VOICES];
        for (int i = 0; i < unique_target_size; i++) {
            rotated_target[i] = unique_target[(i + inversion) % unique_target_size];
        }

        if (x->debug_enabled && inversion < 3) {
            post("DEBUG: Trying inversion %d", inversion);
        }

        // Build matrix for this inversion
        int cost = build_matrix(x, unique_source, unique_source_size,
                               rotated_target, unique_target_size,
                               matrix, output_matrix);

        if (cost < best_cost) {
            best_cost = cost;

            // Find voice leading for this inversion
            int vl_size = find_matrix_vl(x, unique_source, unique_source_size,
                                        rotated_target, unique_target_size,
                                        output_matrix, temp_vl);

            // Copy to best
            *best_vl_size = vl_size;
            for (int i = 0; i < vl_size; i++) {
                best_vl[i] = temp_vl[i];
            }
        }
    }

    x->last_vl_cost = best_cost;

    if (x->debug_enabled) {
        post("DEBUG: Best voice leading cost: %d", best_cost);
    }
}

// Apply voice leading to actual pitches
static void apply_voice_leading(t_voice_leading *x,
                                int *input_pitches, int input_size,
                                voice_pair_t *vl, int vl_size,
                                int *output_pitches, int *output_size) {

    // Create a list of which input pitches are used
    bool used[MAX_VOICES] = {false};
    *output_size = 0;

    // For each voice pair, find the closest input pitch with matching PC
    for (int i = 0; i < vl_size; i++) {
        int source_pc = vl[i].source_note % MODULUS;
        int target_pc = vl[i].target_note % MODULUS;

        // Find unused input pitch with matching source PC
        int best_input_idx = -1;
        int best_distance = VERYLARGENUMBER;

        for (int j = 0; j < input_size; j++) {
            if (used[j]) continue;

            int pitch_pc = input_pitches[j] % MODULUS;
            if (pitch_pc < 0) pitch_pc += MODULUS;

            if (pitch_pc == source_pc) {
                // Calculate distance to target
                int distance = abs(input_pitches[j] - target_pc);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_input_idx = j;
                }
            }
        }

        if (best_input_idx >= 0) {
            used[best_input_idx] = true;

            // Calculate output pitch (keep it close to input)
            int input_pitch = input_pitches[best_input_idx];
            int output_pitch = input_pitch;

            // Find nearest occurrence of target PC
            int input_pc = input_pitch % MODULUS;
            if (input_pc < 0) input_pc += MODULUS;

            // Calculate path
            int path = (target_pc - input_pc + MODULUS) % MODULUS;
            if (path > HALFMODULUS) path -= MODULUS;

            output_pitch = input_pitch + path;
            output_pitches[*output_size] = output_pitch;
            (*output_size)++;

            if (x->debug_enabled) {
                post("DEBUG: Voice %d: %d (PC %d) -> %d (PC %d)",
                     i, input_pitch, input_pc, output_pitch, target_pc);
            }
        }
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
        post("\nDEBUG: ===== Starting Nonbijective Voice Leading =====");
        post("DEBUG: Current chord: [%d %d %d %d]",
             x->current_chord[0], x->current_chord[1],
             x->current_chord[2], x->current_chord[3]);
        post("DEBUG: Target PCs: [%d %d %d %d]",
             x->chord_intervals[0], x->chord_intervals[1],
             x->chord_intervals[2], x->chord_intervals[3]);
    }

    // Convert current chord to PCs
    int source_pcs[MAX_VOICES];
    for (int i = 0; i < x->current_size; i++) {
        source_pcs[i] = x->current_chord[i] % MODULUS;
        if (source_pcs[i] < 0) source_pcs[i] += MODULUS;
    }

    // Find optimal voice leading using nonbijective algorithm
    voice_pair_t best_vl[MAX_MATRIX_SIZE];
    int best_vl_size;

    nonbijective_vl(x, source_pcs, x->current_size,
                    x->chord_intervals, x->chord_size,
                    best_vl, &best_vl_size);

    // Apply voice leading to actual pitches
    int output_chord[MAX_VOICES];
    int output_chord_size;

    apply_voice_leading(x, x->current_chord, x->current_size,
                       best_vl, best_vl_size,
                       output_chord, &output_chord_size);

    if (x->debug_enabled) {
        post("DEBUG: Output chord: [%d %d %d %d]",
             output_chord_size > 0 ? output_chord[0] : 0,
             output_chord_size > 1 ? output_chord[1] : 0,
             output_chord_size > 2 ? output_chord[2] : 0,
             output_chord_size > 3 ? output_chord[3] : 0);
        post("DEBUG: Voice leading cost: %d", x->last_vl_cost);
        post("DEBUG: Root PC: %d (MIDI note: %d)",
             x->root_interval, 48 + x->root_interval);
    }

    // Output results
    t_atom out_list[MAX_VOICES];
    for (int i = 0; i < output_chord_size; i++) {
        SETFLOAT(&out_list[i], output_chord[i]);
    }

    outlet_list(x->x_out_chord, &s_list, output_chord_size, out_list);
    outlet_float(x->x_out_root, (t_float)(48 + x->root_interval));

    if (x->feedback_enabled) {
        memcpy(x->current_chord, output_chord, output_chord_size * sizeof(int));
        x->current_size = output_chord_size;

        if (x->debug_enabled) {
            post("DEBUG: Feedback enabled - updated current chord");
        }
    }
}

// Set current chord (COLD)
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

// Set root interval (COLD)
static void voice_leading_root(t_voice_leading *x, t_floatarg f) {
    int root = (int)f % 12;
    if (root < 0) root += 12;
    x->root_interval = root;

    if (x->debug_enabled) {
        post("voice_leading: root set to %d", x->root_interval);
    }
}

// Set chord structure as intervals from root (HOT)
static void voice_leading_chord(t_voice_leading *x, t_symbol *s, int argc, t_atom *argv) {
    if (argc > MAX_VOICES) {
        pd_error(x, "voice_leading: too many chord intervals (max %d)", MAX_VOICES);
        return;
    }

    x->chord_structure_size = argc;
    for (int i = 0; i < argc; i++) {
        x->chord_structure[i] = (int)atom_getfloat(&argv[i]);
    }

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

    if (x->current_size > 0) {
        voice_leading_calculate(x);
    } else {
        pd_error(x, "voice_leading: no current chord set");
    }
}

// Set target chord intervals (HOT)
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

// Bang
static void voice_leading_bang(t_voice_leading *x) {
    voice_leading_calculate(x);
}

// Constructor
static void *voice_leading_new(void) {
    t_voice_leading *x = (t_voice_leading *)pd_new(voice_leading_class);

    x->x_out_info = outlet_new(&x->x_obj, &s_list);
    x->x_out_chord = outlet_new(&x->x_obj, &s_list);
    x->x_out_root = outlet_new(&x->x_obj, &s_float);

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

    post("voice_leading: initialized (nonbijective dynamic programming)");
    post("  Allows unequal voice counts and smart doubling/omission");
    post("  Two modes: 1) absolute PCs with 'target', 2) root+intervals with 'chord'");
    post("  Outlets: [root] [chord] [info]");

    return (void *)x;
}

// Setup
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

    post("voice_leading external loaded (nonbijective algorithm)");
    post("Usage: [voice_leading]");
    post("  'current <pitches>' - set current chord (any size)");
    post("  'target <pcs>' - set target as absolute pitch classes (any size)");
    post("  'root <pc>' + 'chord <intervals>' - set target as root+intervals");
    post("  'feedback <0|1>' - enable/disable feedback");
    post("  'debug <0|1>' - enable/disable debug output");
    post("Outlets: [root (MIDI)] [chord (list)] [info (list)]");
    post("NEW: Supports unequal voice counts (3-voice to 4-voice, etc.)");
}