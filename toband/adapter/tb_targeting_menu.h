/* TOband の呪文の一覧・アイテムの選択の行から、狙いの画面へ「行の中身」を渡す宣言。
 * 幻想蛮怒の gb_targeting_menu.h と同じ。共通の宣言ヘッダ（native_targeting_api.h）には足さない。 */
#pragma once
void tb_target_menu_book_row(int key,int row,int column,const char *rendered,const char *name,int realm,int spell,int eligible,int selected);
void tb_target_preview_fixed(void);
void tb_target_item_preview(void *actor,int action,const void *object);
void tb_target_item_profile(int action,const void *object);
