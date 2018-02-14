#pragma once

#include <array>
#include <unordered_map>
#include <unordered_set>

extern double prob_ptn3x3[65536][256][4];
extern std::unordered_map<int, std::array<double, 4>> prob_ptn12;
extern std::unordered_map<int, std::array<double, 2>> prob_ptn_rsp;
extern std::unordered_set<int> ladder_ptn[2];
extern std::string working_dir;

class Pattern3x3 {
public:
	int bf;

	Pattern3x3(){ bf = 0x00000000; }
	Pattern3x3(int ibf){ bf = ibf; }
	Pattern3x3& operator=(const Pattern3x3& rhs){ bf = rhs.bf; return *this; }

	// ������.
	// Initialize
	void Clear(){ bf = 0x00000000; }

	// Null������
	// Set null value. =UINT_MAX.
	void SetNull(){ bf = 0xffffffff; }

	// ������̐΂�Ԃ�
	// Return stone color in a direction.
	// dir: 0(N),1(E),2(S),3(W),4(NW),5(NE),6(SE),7(SW),
	//      8(NN),9(EE),10(SS),11(WW)
	int ColorAt(int dir) const{ return (bf >> (2 * dir)) & 3; }

	// ������̐΂��X�V
	// Update stone color in a direction.
	// dir: 0(N),1(E),2(S),3(W),4(NW),5(NE),6(SE),7(SW)
	//      8(NN),9(EE),10(SS),11(WW)
	void SetColor(int dir, int color)
	{
		bf &= ~(3 << (2 * dir));
		bf |= color << (2 * dir);
	}

	// ���ׂĂ̐΂̐F�𔽓]������
	// Flip color of all black/white stones.
	void FlipColor()
	{
		// 0xa = 1010
		// 1. bf & 0x00aaaaaa -> �΂̏��bit
		// 2. 1bit�V�t�g�ŉ���bit�}�X�N�𐶐�
		// 3. �΂̉���bit��XOR�����A11�i���j<->10�i���j��ϊ�
		bf ^= ((bf & 0x00aaaaaa) >> 1);
	}

	// �㉺���E��pl���̐ΐ��𒲂ׂ�
	// Return count of pl's neighbor(NSEW) stones.
	// pl: 0->white, 1->black.
	int StoneCnt(int pl) const
	{
		return 	int(((bf     ) & 3) == (pl + 2)) +
				int(((bf >> 2) & 3) == (pl + 2)) +
				int(((bf >> 4) & 3) == (pl + 2)) +
				int(((bf >> 6) & 3) == (pl + 2));
	}

	// �㉺���E�̋�_���𒲂ׂ�
	// Return count of neighbor (NSEW) blank.
	int EmptyCnt() const
	{
		return 	int(((bf     ) & 3) == 0) +
				int(((bf >> 2) & 3) == 0) +
				int(((bf >> 4) & 3) == 0) +
				int(((bf >> 6) & 3) == 0);
	}

	// �㉺���E�̔ՊO�_���𒲂ׂ�
	// Return count of neighbor (NSEW) outer boundary.
	int EdgeCnt() const
	{
		return 	int(((bf     ) & 3) == 1) +
				int(((bf >> 2) & 3) == 1) +
				int(((bf >> 4) & 3) == 1) +
				int(((bf >> 6) & 3) == 1);
	}

	// pl���̐΂Ɉ͂܂�Ă��邩
	// Return whether it is surrounded by pl's stones.
	bool IsEnclosed(int pl) const
	{
		return (StoneCnt(pl) + EdgeCnt()) == 4;
	}

	// �㉺���E�̃A�^�����X�V
	// Set Atari in each direction (NSEW).
	void SetAtari(bool bn, bool be, bool bs, bool bw){
		//1. �A�^���ɂ���΂�2�ċz�_bit(10)������
		bf &= ~((bn << 25) | (be << 27) | (bs << 29) | (bw << 31));
		//2. �A�^��bit(01)��ǉ�
		bf |= (bn << 24) | (be << 26) | (bs << 28) | (bw << 30);
	}

	// �㉺���E�̃A�^��������
	// Cancel Atari in each direction (NSEW).
	void CancelAtari(bool bn, bool be, bool bs, bool bw)
	{
		bf &= ~((bn << 24) | (be << 26) | (bs << 28) | (bw << 30));
	}

	// �S�ẴA�^���̐΂���������
	// Cancel Atari in all directions.
	void ClearAtari()
	{
		//0xa = 1010, 0xf = 1111
		bf &= 0xaaffffff;
	}

	// ������̃A�^���̗L����Ԃ�
	// Return whether the next stone is Atari.
	bool IsAtari(int dir) const
	{
		return (bf >> (24 + 2 * dir)) & 1;
	}

	// �����ꂩ���A�^�����𒲂ׂ�
	// Return whether any of next stones is Atari.
	bool IsAtari() const
	{
		//5 5 = 0101 0101
		return ((bf >> 24) & 0x00000055) != 0;
	}

	// �㉺���E�̂Q�ċz�_���X�V
	// Set pre-Atari (liberty = 2) in each direction (NSEW).
	void SetPreAtari(bool bn, bool be, bool bs, bool bw)
	{
		//1. 2�ċz�_�ɂ���΂̃A�^��bit(01)������
		bf &= ~((bn << 24) | (be << 26) | (bs << 28) | (bw << 30));
		//2. 2�ċz�_bit(10)��ǉ�
		bf |= (bn << 25) | (be << 27) | (bs << 29) | (bw << 31);
	}

	// �㉺���E�̂Q�ċz�_������
	// Cancel pre-Atari in each direction (NSEW).
	void CancelPreAtari(bool bn, bool be, bool bs, bool bw)
	{
		bf &= ~((bn << 25) | (be << 27) | (bs << 29) | (bw << 31));
	}

	// �S�Ă̂Q�ċz�_�̐΂���������
	// Cancel pre-Atari in all directions.
	void ClearPreAtari()
	{
		//0x5 = 0101, 0xf = 1111
		bf &= 0x55ffffff;
	}

	// ������̂Q�ċz�_�̗L����Ԃ�
	// Return whether the next stone is pre-Atari.
	bool IsPreAtari(int dir) const
	{
		return (bf >> (24 + 2 * dir)) & 2;
	}

	// �����ꂩ���Q�ċz�_���𒲂ׂ�
	// Return whether any of next stones is pre-Atari.
	bool IsPreAtari() const
	{
		//0xaa = 1010 1010
		return ((bf >> 24) & 0x000000aa) != 0;
	}

	// pl���ɂ����č��@��ł��邩�𒲂ׂ�
	// Return whether pl's move into here is legal.
	bool IsLegal(int pl) const
	{
		// 1. ��_���אڂ���Ƃ��͍��@��
		//    Legal if a blank vertex is adjacent.
		if(EmptyCnt() != 0) return true;

		int color_cnt[2] = {0, 0};	//0->white, 1->black
		int atari_cnt[2] = {0, 0};
		int pl_i;

		// 2. �㉺���E�̐Ώ��𒲂ׂ�
		//    Count neighbor stones and Atari.
		for (int i=0;i<4;++i) {
			pl_i = ColorAt(i) - 2;	//0->����, 1->����
			if(pl_i >= 0){
				++color_cnt[pl_i];
				if(IsAtari(i))	++atari_cnt[pl_i];
			}
		}

		// 3. �G�΂��A�^���̂Ƃ��A�܂���
		//    �A�^���łȂ����΂�����Ƃ����@��
		//    Legal if opponent's stone is Atari,
		//    or any of my stones is NOT Atari.
		return (	atari_cnt[int(pl==0)] != 0 ||
					atari_cnt[pl] < color_cnt[pl]	);
	}

	// pl���ɂ����Ċ�`�ł��邩�𒲂ׂ�
	// Return whether here is pl's eye shape.
	bool IsEyeShape(int pl) const
	{
		// 1. pl���̐΂Ɉ͂܂�Ă��Ȃ��Ƃ��͊�`�łȂ�
		//    Return false if here is not enclosed by pl's stones.
		if(!IsEnclosed(pl)) return false;

		int diag_cnt[4] = {0, 0, 0, 0};

		// 2. �i�i���ʒu�̐΂𒲂ׂ�
		//    Count stones in diagonal directions.
		for (int i=4;i<8;++i) {
			++diag_cnt[ColorAt(i)];
		}

		// 3. �����ڂłȂ��Ƃ���`
		//    �i�i���ʒu�̓G�΁{�ՊO��2�ȏ�̂Ƃ��A�����ڂƂȂ�
		//    Return true if not false-eye pattern as follows.
		//
		//	  XO.  XO#
		//    O.O  O.#
		//    XO.  .O#
		return (diag_cnt[int(pl==0) + 2] + int(diag_cnt[1] > 0)) < 2;
	}

	// pl���ɂ����ăV�`���E�̉\���̂���p�^�[�����𒲂ׂ�
	// Return whether the pattern matches 'Ladder'. (quick judgment)
	bool IsLadder(int pl) const
	{
		// �V�`���E�̉\���̂���p�o�p�^�[��
		// Frequent pattern that may be Ladder.
		//
		// (1)�Ւ[�̓���p�^�[��
		//    Edge ladder.
		// #.X
		// #.O <- Atari
		// #.X
		//
		// (2)�K�i��ɂȂ�ʏ�p�^�[��
		//    Normal ladder.
		// ..X
		// ..O <- Atari
		// .XO

		//1. �אڂ����_����2�łȂ� or ���ΐ���1�łȂ��ꍇ�A�Y�����Ȃ�
		//   Return false if blank count is 2,
		//   or my stone count is 1 in neighbor position.
		if(EmptyCnt() != 2 || StoneCnt(pl) != 1) return false;

		int my_color = pl + 2;
		int her_color = int(pl == 0) + 2;

		// 2. (1)�̃p�^�[����
		//    Check whether it matches pattern (1).
		if(EdgeCnt() != 0){
			for(int i=0;i<4;++i){
				// ���΂��A�^���ŁA���Α����Ւ[�̂Ƃ��̓V�`���E
				if(	ColorAt(i) == my_color &&
					IsAtari(i) &&
					ColorAt(i + 2 * (2 * int(i < 2) - 1)) == 1)	//i�̔��Α����Ւ[��
				{
					return true;
				}
			}
		}
		// 2. (2)�̃p�^�[����
		//    Check whether it matches pattern (2).
		else{
			int color_at[8];
			int atari_at[4];
			for(int i=0;i<8;++i){
				color_at[i] = (bf >> (2 * i)) & 3;
			}
			for(int i=0;i<4;++i){
				atari_at[i] = (bf >> (24 + 2 * i)) & 3;
			}

			const int ladder_ptn[8][5] = {	{0,1,4,6,7}, {0,3,5,6,7},
											{1,0,6,4,7}, {1,2,5,4,7},
											{2,1,7,4,5}, {2,3,6,4,5},
											{3,0,7,5,6}, {3,2,4,5,6}	};

			for(int i=0;i<8;++i){
				for(int j=0;j<5;++j){
					int dir = ladder_ptn[i][j];
					if(j == 0){
						if(	color_at[dir] != my_color ||
							atari_at[dir] != 1) break;
					}
					else if(j == 1){
						if(	color_at[dir] != her_color ||
							atari_at[dir] == 2) break;
					}
					else if(j == 2){
						if(color_at[dir] != her_color) break;
					}
					else{
						if(color_at[dir] == my_color) break;
					}

					if(j == 4) return true;
				}
			}
		}

		// 3. �V�`���E�p�^�[���ɊY�����Ȃ�
		//    Return false in other cases.
		return false;
	}

	// pl���̊m���p�����[�^�����߂�
	// Returns pl's probability.
	double GetProb3x3(int pl, bool is_restored) const
	{
		int stone_bf = bf & 0x0000ffff;
		int atari_bf = bf >> 24;

		return prob_ptn3x3[stone_bf][atari_bf][pl + 2 * (int)is_restored];
	}

	// �΁E�A�^���������v����90����]����
	// Return Pattern3x3 which is rotated clockwise by 90 degrees.
	Pattern3x3 Rotate() const
	{
		//3 = 0011, c = 1100, f = 1111
		Pattern3x3 rot_ptn(
			((bf << 2) & 0xfcfcfcfc) |
			((bf >> 6) & 0x03030303)
		);

		return rot_ptn;
	}

	// �΁E�A�^���������f���]����
	// Return Pattern3x3 which is horizontally inverted.
	Pattern3x3 Invert() const
	{
		//3 = 0011, c = 1100
		Pattern3x3 mir_ptn(
			(bf & 0x33330033) 			|
			((bf << 4) & 0xc0c000c0)	|
			((bf >> 4) & 0x0c0c000c)	|
			((bf << 2) & 0x0000cc00)	|
			((bf >> 2) & 0x00003300)
		);

		return mir_ptn;
	}

	// �Ώ̑���ɂ��ŏ��̃n�b�V���l�����߂�
	// Return Pattern3x3 which has the minimum bf.
	Pattern3x3 GetMin() const
	{
		Pattern3x3 tmp_ptn(bf);
		Pattern3x3 min_ptn = tmp_ptn;

		for (int i=0;i<2;++i) {
			for (int j=0;j<4;++j) {
				if (tmp_ptn.bf < min_ptn.bf){
					min_ptn = tmp_ptn;
				}
				tmp_ptn = tmp_ptn.Rotate();
			}
			tmp_ptn = tmp_ptn.Invert();
		}

		return min_ptn;
	}

};

void ImportProbPtn3x3();
