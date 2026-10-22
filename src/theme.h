/******************************************************************************
 *  theme.h - 界面主题
 *  竹影清风PE文件资源管理器   竹影清风制作  FirTeam出品
 *
 *  ────────────────────────────────────────────────────────────────────────
 *  纯白简约风格，配色与尺寸**取自 Rust + iced 版的「竹影清风PE工具合集」**，
 *  保证两套程序观感完全一致。
 *
 *  iced 版对应值（0.0~1.0 浮点 → 这里换算成 RGB）：
 *      C_BG          #F4F5F6      窗口 / 内容区背景
 *      C_CARD        #FFFFFF      卡片 / 标题栏 / 列表底
 *      C_SIDE        #F8F9FA      侧栏 / 列头 / 状态栏
 *      C_TEXT        #1D1E22      主文字
 *      C_SUB         #65676D      次文字
 *      C_FAINT       #A2A5AA      弱文字
 *      C_LINE        #E7E8EA      分隔线
 *      C_LINE_STRONG #CCCED1      强分隔线
 *      C_HOVER_BG    #F1F2F4      悬停底
 *      C_SEL_BG      #E7EFFC      选中底
 *      C_ACCENT      #2673E8      强调色
 *      C_ACCENT_SOFT #5A92EE      强调柔色
 *      C_REQ         #E04545      危险 / 删除
 *      C_WIN_HOVER   #E5E7EA      标题栏按钮悬停
 *      C_CLOSE_HOVER #C42B1C      关闭按钮悬停
 *
 *  尺寸：标题栏 38px、标题栏按钮宽 46px、侧栏 196px（均与 iced 版同值）
 * ────────────────────────────────────────────────────────────────────────
 ******************************************************************************/
#pragma once
#include <windows.h>

/* ============================ 配色 ============================ */
#define T_BG          RGB(0xF4, 0xF5, 0xF6)   /* #F4F5F6 窗口 / 内容区背景 */
#define T_CARD        RGB(0xFF, 0xFF, 0xFF)   /* #FFFFFF 卡片 / 标题栏     */
#define T_SIDE        RGB(0xF8, 0xF9, 0xFA)   /* #F8F9FA 侧栏 / 列头 / 状态栏 */
#define T_TEXT        RGB(0x1D, 0x1E, 0x22)   /* #1D1E22 主文字            */
#define T_SUB         RGB(0x65, 0x67, 0x6D)   /* #65676D 次文字            */
#define T_FAINT       RGB(0xA2, 0xA5, 0xAA)   /* #A2A5AA 弱文字            */
#define T_LINE        RGB(0xE7, 0xE8, 0xEA)   /* #E7E8EA 分隔线            */
#define T_LINE_STRONG RGB(0xCC, 0xCE, 0xD1)   /* #CCCED1 强分隔线          */
#define T_HOVER       RGB(0xF1, 0xF2, 0xF4)   /* #F1F2F4 悬停底            */
#define T_SEL         RGB(0xE7, 0xEF, 0xFC)   /* #E7EFFC 选中底            */
#define T_ACCENT      RGB(0x26, 0x73, 0xE8)   /* #2673E8 强调色            */
#define T_ACCENT_SOFT RGB(0x5A, 0x92, 0xEE)   /* #5A92EE 强调柔色          */
#define T_DANGER      RGB(0xE0, 0x45, 0x45)   /* #E04545 危险 / 删除       */
#define T_WIN_HOVER   RGB(0xE5, 0xE7, 0xEA)   /* #E5E7EA 窗口按钮悬停      */
#define T_CLOSE_HOVER RGB(0xC4, 0x2B, 0x1C)   /* #C42B1C 关闭按钮悬停      */

/* ============================ 尺寸（与 iced 版同值） ============================ */
#define T_TITLE_H     38                      /* 标题栏高                  */
#define T_WINBTN_W    46                      /* 标题栏按钮宽              */
#define T_SIDE_W      196                     /* 侧栏宽                    */
