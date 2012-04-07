/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU Lesser General Public License, version 3
 * http://www.gnu.org/licenses/lgpl-3.0.html
 *
 * $Revision$
 * $Id$
 * $HeadURL$
 */

//#include "sdk_precomp.h"
//#ifndef CB_PRECOMP
//    #include "globals.h"
//#endif

#include "sdk_precomp.h"
#include "cbstyledtextctrl.h"
#ifndef CB_PRECOMP
    #include <wx/string.h>
    #include <wx/timer.h>

    #include "editorbase.h" // DisplayContextMenu
    #include "prep.h" // platform::gtk
    #include "pluginmanager.h"
#endif

#include "cbdebugger_interfaces.h"
#include "debuggermanager.h"

static const wxString s_leftBrace(_T("([{'\""));
static const wxString s_rightBrace(_T(")]}'\""));
static const int s_indicHighlight(20);

std::map<int, std::set<int> > cbStyledTextCtrl::CharacterLexerStyles;
std::map<int, std::set<int> > cbStyledTextCtrl::StringLexerStyles;
std::map<int, std::set<int> > cbStyledTextCtrl::PreprocessorLexerStyles;
std::map<int, std::set<int> > cbStyledTextCtrl::CommentLexerStyles;

BEGIN_EVENT_TABLE(cbStyledTextCtrl, wxScintilla)
    EVT_CONTEXT_MENU(cbStyledTextCtrl::OnContextMenu)
    EVT_KILL_FOCUS  (cbStyledTextCtrl::OnKillFocus)
    EVT_MIDDLE_DOWN (cbStyledTextCtrl::OnMouseMiddleDown)
    EVT_SET_FOCUS   (cbStyledTextCtrl::OnSetFocus)
    EVT_KEY_DOWN    (cbStyledTextCtrl::OnKeyDown)
    EVT_KEY_UP      (cbStyledTextCtrl::OnKeyUp)
    EVT_LEFT_UP     (cbStyledTextCtrl::OnMouseLeftUp)
END_EVENT_TABLE()

cbStyledTextCtrl::cbStyledTextCtrl(wxWindow* pParent, int id, const wxPoint& pos, const wxSize& size, long style) :
    wxScintilla(pParent, id, pos, size, style),
    m_pParent(pParent),
    m_lastFocusTime(0L),
    m_bracePosition(wxSCI_INVALID_POSITION),
    m_lastPosition(wxSCI_INVALID_POSITION),
    m_tabSmartJump(false)
{
    //ctor
    m_braceShortcutState = false;
}

cbStyledTextCtrl::~cbStyledTextCtrl()
{
    //dtor
}

// events

void cbStyledTextCtrl::OnKillFocus(wxFocusEvent& event)
{
    // cancel auto-completion list when losing focus
    if ( AutoCompActive() )
        AutoCompCancel();

    if ( CallTipActive() )
        CallTipCancel();

    event.Skip();
}

void cbStyledTextCtrl::OnSetFocus(wxFocusEvent& event)
{
    // store timestamp for use in cbEditor::GetControl()
    // don't use event.GetTimeStamp(), because the focus event has no timestamp !
    m_lastFocusTime = wxGetLocalTimeMillis();
    event.Skip();
}

void cbStyledTextCtrl::OnContextMenu(wxContextMenuEvent& event)
{
    if (m_pParent)
    {
        if ( EditorBase* pParent = dynamic_cast<EditorBase*>(m_pParent) )
        {
            const bool is_right_click = event.GetPosition() != wxDefaultPosition;
            const wxPoint mp(is_right_click ? event.GetPosition() : wxDefaultPosition);
            pParent->DisplayContextMenu(mp, mtEditorManager);
        }
        else
            event.Skip();
    }
}

void cbStyledTextCtrl::OnMouseMiddleDown(wxMouseEvent& event)
{
    if (platform::gtk == false) // only if OnMouseMiddleDown is not already implemented by the OS
    {
        int pos = PositionFromPoint(wxPoint(event.GetX(), event.GetY()));

        if (pos == wxSCI_INVALID_POSITION)
            return;

        int start = GetSelectionStart();
        int end   = GetSelectionEnd();

        const wxString s = GetSelectedText();

        if (pos < GetCurrentPos())
        {
            start += s.length();
            end += s.length();
        }

        InsertText(pos, s);
        SetSelectionVoid(start, end);
    }
}

void cbStyledTextCtrl::OnKeyDown(wxKeyEvent& event)
{
    bool emulateDwellStart = false;

    m_lastSelectedText = GetSelectedText();

    switch (event.GetKeyCode())
    {
        case _T('I'):
        {
            if (event.GetModifiers() == wxMOD_ALT)
                m_braceShortcutState = true;
            break;
        }

        case WXK_TAB:
        {
            if (m_tabSmartJump && !(event.ControlDown() || event.ShiftDown() || event.AltDown()))
            {
                if (!AutoCompActive() && m_bracePosition != wxSCI_INVALID_POSITION)
                {
                    m_lastPosition = GetCurrentPos();
                    GotoPos(m_bracePosition);

                    // Need judge if it's the final brace
                    HighlightRightBrace();
                    if (!m_tabSmartJump && CallTipActive())
                        CallTipCancel();
                    return;
                }
            }
        }
        break;

        case WXK_BACK:
        {
            if (m_tabSmartJump)
            {
                if (!(event.ControlDown() || event.ShiftDown() || event.AltDown()))
                {
                    const int pos = GetCurrentPos();
                    const int index = s_leftBrace.Find((wxChar)GetCharAt(pos - 1));
                    if (index != wxNOT_FOUND && (wxChar)GetCharAt(pos) == s_rightBrace.GetChar(index))
                    {
                        CharRight();
                        DeleteBack();
                    }
                }
                else if (m_lastPosition != wxSCI_INVALID_POSITION && event.ControlDown())
                {
                    GotoPos(m_lastPosition);
                    m_lastPosition = wxSCI_INVALID_POSITION;
                    return;
                }
            }
        }
        break;

        case WXK_RETURN:
        case WXK_ESCAPE:
        {
            if (m_tabSmartJump)
                m_tabSmartJump = false;
        }
        break;

        case WXK_CONTROL:
        {
            EmulateDwellStart();
            emulateDwellStart = true;
        }
        break;
    }

    if (event.ControlDown() && !emulateDwellStart)
        EmulateDwellStart();

    event.Skip();
}

void cbStyledTextCtrl::OnKeyUp(wxKeyEvent& event)
{
    const int keyCode = event.GetKeyCode();
    switch (keyCode)
    {
        case _T('['):   // [ {
        case _T('\''):  // ' "
#ifdef __WXMSW__
        case _T('9'):   // ( for wxMSW
#else
        case _T('('):   // ( for wxGTK
#endif
        {
            if (!AllowTabSmartJump())
                break;

            wxChar ch = keyCode;
            if (event.ShiftDown())
            {
                if (keyCode == _T('\''))
                    ch = _T('"');
                else if (keyCode == _T('9'))
                    ch = _T('(');
                else if (keyCode == _T('['))
                    ch = _T('{');
            }

            int index = s_leftBrace.Find(ch);
            if (index != wxNOT_FOUND && (wxChar)GetCharAt(GetCurrentPos()) == s_rightBrace.GetChar(index))
            {
                const int pos = GetCurrentPos();
                if (pos != wxSCI_INVALID_POSITION)
                {
                    m_tabSmartJump = true;
                    m_bracePosition = pos;
                }
            }
            else if (keyCode == _T('\'')) // ' "
                m_tabSmartJump = false;
        }
        break;

        case _T(']'):   // ] }
#ifdef __WXMSW__
        case _T('0'):   // ) for wxMSW
#else
        case _T(')'):   // ) for wxGTK
#endif
        {
            if (!AllowTabSmartJump())
                break;
            if (keyCode == _T('0') && !event.ShiftDown())
                break;
            m_tabSmartJump = false;
        }
        break;
    }

    HighlightRightBrace();
    event.Skip();
}

void cbStyledTextCtrl::OnMouseLeftUp(wxMouseEvent& event)
{
    HighlightRightBrace();
    event.Skip();
}

bool cbStyledTextCtrl::IsCharacter(int style)
{
    return CharacterLexerStyles[GetLexer()].find(style) != CharacterLexerStyles[GetLexer()].end();
}

bool cbStyledTextCtrl::IsString(int style)
{
    return StringLexerStyles[GetLexer()].find(style) != StringLexerStyles[GetLexer()].end();
}

bool cbStyledTextCtrl::IsPreprocessor(int style)
{
    return PreprocessorLexerStyles[GetLexer()].find(style) != PreprocessorLexerStyles[GetLexer()].end();
}

bool cbStyledTextCtrl::IsComment(int style)
{
    return CommentLexerStyles[GetLexer()].find(style) != CommentLexerStyles[GetLexer()].end();
}

void cbStyledTextCtrl::CallTipCancel()
{
    if (!m_tabSmartJump)
        wxScintilla::CallTipCancel();
}

bool cbStyledTextCtrl::IsBraceShortcutActive()
{
    bool state = m_braceShortcutState;
    m_braceShortcutState = false;
    return state;
}

bool cbStyledTextCtrl::AllowTabSmartJump()
{
    const int pos = GetCurrentPos();
    if (pos == wxSCI_INVALID_POSITION)
        return false;

    const int style = GetStyleAt(pos);
    if (IsString(style) || IsCharacter(style) || IsComment(style) || IsPreprocessor(style))
        return !m_tabSmartJump;
    return true;
}

void cbStyledTextCtrl::HighlightRightBrace()
{
    if (m_bracePosition == wxSCI_INVALID_POSITION)
        return;

    int pos = GetCurrentPos();
    if (pos == wxSCI_INVALID_POSITION)
        return;

    const static wxColour caretForeground = GetCaretForeground();
    const static int caretWidth = GetCaretWidth();
    const int curLine = GetCurrentLine();
    const int len = GetLength();

    if (m_tabSmartJump && (curLine == LineFromPosition(m_bracePosition)))
    {
        SetIndicatorCurrent(s_indicHighlight);
        IndicatorClearRange(GetLineIndentPosition(curLine), GetLineEndPosition(curLine));
        do
        {
            if (pos >= len)
                break;

            wxString cur((wxChar)GetCharAt(pos));
            if (cur == _T("\n"))
                break;

            int style = GetStyleAt(pos);
            if (IsComment(style))
                continue;

            if (IsString(style) || IsCharacter(style))
            {
                const int nextOne = (pos == len) ? GetStyleAt(pos) : GetStyleAt(pos + 1);
                if (IsCharacter(nextOne) || IsString(nextOne))
                    continue;
            }

            if (s_rightBrace.Contains(cur))
            {
                SetCaretForeground(wxColour(255, 0, 0));
                SetCaretWidth(caretWidth + 1);

                IndicatorSetForeground(s_indicHighlight, wxColour(80, 236, 120));
                IndicatorSetStyle(s_indicHighlight, wxSCI_INDIC_HIGHLIGHT);
#ifndef wxHAVE_RAW_BITMAP
                IndicatorSetUnder(s_indicHighlight, true);
#endif
                SetIndicatorCurrent(s_indicHighlight);
                IndicatorFillRange(pos, 1);
                m_bracePosition = pos + 1;
                return;
            }
        }
        while (++pos);
    }

    m_bracePosition = wxSCI_INVALID_POSITION;
    m_lastPosition = wxSCI_INVALID_POSITION;
    m_tabSmartJump = false;
    SetIndicatorCurrent(s_indicHighlight);
    IndicatorClearRange(0, len);

    SetCaretForeground(caretForeground);
    SetCaretWidth(caretWidth);
}

void cbStyledTextCtrl::EmulateDwellStart()
{
    EditorBase *editor = static_cast<EditorBase*>(m_pParent);

    CodeBlocksEvent event(cbEVT_EDITOR_TOOLTIP);
    wxPoint pt(ScreenToClient(wxGetMousePosition()));
    event.SetX(pt.x);
    event.SetY(pt.y);
    int pos = PositionFromPoint(pt);
    int style = GetStyleAt(pos);
    event.SetInt(style);
    event.SetEditor(editor);
    Manager::Get()->GetPluginManager()->NotifyPlugins(event);
}

void cbStyledTextCtrl::EnableTabSmartJump(bool enable)
{
    m_tabSmartJump = enable;
    m_bracePosition = GetCurrentPos();
    HighlightRightBrace();
}

std::map<int, std::set<int> > &cbStyledTextCtrl::GetCharacterLexerStyles()
{
    return CharacterLexerStyles;
}

std::map<int, std::set<int> > &cbStyledTextCtrl::GetStringLexerStyles()
{
    return StringLexerStyles;
}

std::map<int, std::set<int> > &cbStyledTextCtrl::GetPreprocessorLexerStyles()
{
    return PreprocessorLexerStyles;
}

std::map<int, std::set<int> > &cbStyledTextCtrl::GetCommentLexerStyles()
{
    return CommentLexerStyles;
}
