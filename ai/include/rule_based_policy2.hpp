#pragma once
#include "contrast/game_state.hpp"
#include "contrast/move.hpp"
#include "contrast/move_list.hpp"
#include <vector>
#include <random>

namespace contrast_ai {
/*
ルールベースポリシー改良版
基本戦略:
1. 勝ち筋を見つけたら即決する
2. 相手の即勝ち筋は必ず潰す
3. 自陣と敵陣の間の空間総数で偶奇管理し、奇数なら直進・偶数ならタイルで崩す
4. 高さが乱れた敵列の横腹にタイルを差し込み主導権を奪う
5. 自陣の横一線を端から押し上げてラインを維持する
6. 相手の高い列の正面を同じ高さまで引き上げて前線を固定する
*/
// checkImmediateWin()       → 勝てるなら即座に決める
// blockImmediateThreat()    → 相手の即勝ちルートを塞ぐ
// paritySkirmishControl()   → 空間総数の偶奇を管理し直進 or 敵目前へのタイル設置を切り替える
// interdictRowFormation()   → 乱れた列の脇にタイルを差し込み崩す
// prioritizeLeadPiece()     → 端列から横一線のラインを押し上げる
// outflankStraightRunner()  → 相手の高列正面を引き上げて押し返す

class RuleBasedPolicy2 {
public:
  RuleBasedPolicy2();
  contrast::Move pick(const contrast::GameState& s);
  
private:
  std::mt19937 rng_;
  
  bool checkImmediateWin(const contrast::GameState& s, const contrast::Move& m) const;
  bool blockImmediateThreat(const contrast::GameState& s,
                            contrast::Player opponent,
                            const contrast::MoveList& moves,
                            contrast::Move& out_move) const;
  bool paritySkirmishControl(const contrast::GameState& s,
                             contrast::Player me,
                             contrast::Player opponent,
                             const contrast::MoveList& moves,
                             contrast::Move& out_move) const;
  bool outflankStraightRunner(const contrast::GameState& s,
                              contrast::Player me,
                              contrast::Player opponent,
                              const contrast::MoveList& moves,
                              contrast::Move& out_move) const;
  bool interdictRowFormation(const contrast::GameState& s,
                             contrast::Player me,
                             contrast::Player opponent,
                             const contrast::MoveList& moves,
                             contrast::Move& out_move) const;
  bool prioritizeLeadPiece(const contrast::GameState& s,
                           contrast::Player me,
                           const contrast::MoveList& moves,
                           contrast::Move& out_move) const;


  contrast::Move fallbackByScore(const contrast::GameState& s,
                                 contrast::Player me,
                                 const contrast::MoveList& moves) const;
  
};

} // namespace contrast_ai
