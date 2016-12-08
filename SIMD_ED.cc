#include "SIMD_ED.h"

int count_ID_length_sse(__m128i bit_mask, int start_pos , int total_length) {
	__m128i shifted_mask = shift_left_sse1(bit_mask, start_pos);
	
	cout << "start_pos: " << start_pos << " ";
	print128_bit(shifted_mask);

	unsigned long *byte_cast = (unsigned long*) &shifted_mask;
	int length_result = 0;
	
	for (int i = 0; i < (total_length - start_pos) / 8 * sizeof(unsigned long); i++) {
		int id_length = _tzcnt_u64(byte_cast[i]);

		if (id_length == 0 && byte_cast[i] == 0) {
			id_length = 8 * sizeof(unsigned long);
			length_result += id_length;
		}
		else {
			length_result += id_length;
			break;
		}
	}

	cout << "length result: " << length_result << endl;

	if (length_result < total_length - start_pos)
		return length_result;
	else
		return total_length - start_pos;
}

SIMD_ED::SIMD_ED() {
	ED_t = 0;

	hamming_masks = NULL;

	cur_ED = NULL;
	start = NULL;
	end = NULL;

	mid_lane = 0;
	total_lanes = 0;
}

SIMD_ED::~SIMD_ED() {
	if (total_lanes != 0) {
		delete [] hamming_masks;
		delete [] cur_ED;

		for (int i = 0; i < total_lanes; i++) {
			delete [] start[i];
			delete [] end[i];
		}

		delete [] start;
		delete [] end;

		total_lanes = 0;
	}
}

void SIMD_ED::init(int ED_threshold) {
	if (total_lanes != 0)
		this->~SIMD_ED();

	ED_t = ED_threshold;
	total_lanes = 2 * ED_t + 3;
	mid_lane = ED_t + 1;

	hamming_masks = new __m128i [total_lanes];

	cur_ED = new int[total_lanes];
	ED_info = new ED_INFO[ED_t];

	start = new int* [total_lanes];
	end = new int* [total_lanes];

	for (int i = 0; i < total_lanes; i++) {
		start[i] = new int [ED_t]();
		end[i] = new int [ED_t]();
	}
}

void SIMD_ED::load_reads(char *read, char *ref, int length) {
	buffer_length = length;
	
	if (length > _MAX_LENGTH_)
		length = _MAX_LENGTH_;

	strncpy(A, read, length);

	sse3_convert2bit1(A, A_bit0_t, A_bit1_t);
	strncpy(B, ref, length);
	sse3_convert2bit1(B, B_bit0_t, B_bit1_t);

	cout << "A: " << A  << endl;
	cout << "B: " << B  << endl;

	__m128i *A0 = (__m128i*) A_bit0_t;
	__m128i *A1 = (__m128i*) A_bit1_t;
	__m128i *B0 = (__m128i*) B_bit0_t;
	__m128i *B1 = (__m128i*) B_bit1_t;

	for (int i = 1; i < total_lanes - 1; i++) {
		__m128i shifted_A0 = *A0;
		__m128i shifted_A1 = *A1;
		__m128i shifted_B0 = *B0;
		__m128i shifted_B1 = *B1;

		int shift_amount = abs(i - mid_lane);

		if (i < mid_lane) {
			shifted_A0 = shift_left_sse1(shifted_A0, shift_amount);
			shifted_A1 = shift_left_sse1(shifted_A1, shift_amount);
		}
		else if (i > mid_lane) {
			shifted_B0 = shift_left_sse1(shifted_B0, shift_amount);
			shifted_B1 = shift_left_sse1(shifted_B1, shift_amount);
		}

		__m128i mask_bit0 = _mm_xor_si128(shifted_A0, shifted_B0);
		__m128i mask_bit1 = _mm_xor_si128(shifted_A1, shifted_B1);

		hamming_masks[i] = _mm_or_si128(mask_bit0, mask_bit1);

		cout << "hamming_masks[" << i << "]: ";
		print128_bit(hamming_masks[i]);
		cout << endl;
	}
}

void SIMD_ED::reset() {
	ED_pass = false;
	for (int i = 1; i < total_lanes - 1; i++) {
		int ED = abs(i - mid_lane);
		cur_ED[i] = ED;
		//start[i][ED] = ED;
		//end[i][ED] = ED;
	}
}

void SIMD_ED::run() {
	int length = count_ID_length_sse(hamming_masks[mid_lane], 0, buffer_length);

	cout << "length: " << length << endl;

	end[mid_lane][0] = length;
	cur_ED[mid_lane] = 1;
	
	for (int e = 1; e < ED_t; e++) {
		for (int l = 1; l < total_lanes - 1; l++) {
			if (cur_ED[l] == e) {
				cout << "e: " << e << " l: " << l << endl;

				// Find the largest starting position
				int max_start = end[l][e-1] + 1;
				if (end[l-1][e-1] > max_start)
					max_start = end[l-1][e-1];
				if (end[l+1][e-1] > max_start)
					max_start = end[l+1][e-1];

				// Find the length of identical string
				length = count_ID_length_sse(hamming_masks[l], max_start, buffer_length);

				start[l][e] = max_start;
				end[l][e] = max_start + length;

				cout << "start[" << l << "][" << e << "]: " << start[l][e];
				cout << "   end[" << l << "][" << e << "]: " << end[l][e] << endl;

				if (end[l][e] == buffer_length - 1) {
					final_lane_idx = l;
					final_ED = e;
					ED_pass = true;
					
					break;
				}

				cur_ED[l]++;
			}

		}

		if (ED_pass)
			break;
	}
}

bool SIMD_ED::check_pass() {
	return ED_pass;
}

void SIMD_ED::backtrack() {
	int lane_idx = final_lane_idx;
	int ED_probe = final_ED;

	while (ED_probe > abs(lane_idx - mid_lane) ) {
		int match_count = end[lane_idx][ED_probe] - start[lane_idx][ED_probe];
		ED_info[ED_probe].id_length = match_count;
		
		//int next_lane_idx;
		cout << "start[" << lane_idx << "][" << ED_probe << "]: " << start[lane_idx][ED_probe];
		cout << "   end[" << lane_idx << "][" << ED_probe - 1 << "]: " << end[lane_idx][ED_probe - 1] << endl;

		if (start[lane_idx][ED_probe] == (end[lane_idx][ED_probe - 1] + 1) ) {
			ED_info[ED_probe].type = MISMATCH;
		}
		else if (start[lane_idx][ED_probe] == end[lane_idx - 1][ED_probe - 1]) {
			lane_idx = lane_idx - 1;
			ED_info[ED_probe].type = A_INS;
		}
		else if (start[lane_idx][ED_probe] == end[lane_idx + 1][ED_probe - 1]) {
			lane_idx = lane_idx + 1;
			ED_info[ED_probe].type = B_INS;
		}
		else
			cerr << "Error! No lane!!" << endl;
		
		ED_probe--;
	}

	int match_count = end[lane_idx][ED_probe] - start[lane_idx][ED_probe];
	ED_info[ED_probe].id_length = match_count;

	if (lane_idx < mid_lane) {
		for (int i = mid_lane - lane_idx; i > 0; i--) {
			ED_info[ED_probe].type = B_INS;
			ED_info[ED_probe - 1].id_length = 0;
		}
	}
	else if (lane_idx > mid_lane) {
		for (int i = lane_idx - mid_lane; i > 0; i--) {
			ED_info[ED_probe].type = A_INS;
			ED_info[ED_probe - 1].id_length = 0;
		}
	}

}

int SIMD_ED::get_ED() {
	return final_ED;
}

string SIMD_ED::get_CIGAR() {
	string CIGAR;
	CIGAR = to_string(ED_info[0].id_length);
	for (int i = 1; i <= final_ED; i++) {
		switch (ED_info[i].type) {
		case MISMATCH:
			CIGAR += 'M';
			break;
		case A_INS:
			CIGAR += 'I';
			break;
		case B_INS:
			CIGAR += 'D';
			break;
		}

		CIGAR += to_string(ED_info[i].id_length);
	}

	return CIGAR;
}

