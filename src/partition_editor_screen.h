#ifndef PARTITION_EDITOR_SCREEN_H_
#define PARTITION_EDITOR_SCREEN_H_

#include <xtl.h>
#include "dialog_presenter.h"
#include "filesystem_cache.h"
#include "layout.h"
#include "number_pad.h"
#include "partition_table_session.h"
#include "settings.h"

// What the editor changes and reads: they must outlive it
struct EditorSources
{
    PartitionTableSession &session;
    FilesystemCache &filesystems;
    const Settings &settings;
};

// One-off feedback in a panel footer
struct FooterMessage
{
    LPCWSTR text; // NULL: none, the footer shows the controls
    Color color;
};

// The editor's rows, top to bottom
enum EditField
{
    EDIT_ENABLED = 0,
    EDIT_START,
    EDIT_SIZE,
    EDIT_END,
    EDIT_STEP,
    EDIT_FILL,
    EDIT_BACK,
    EDIT_FIELD_COUNT
};

// Edits one partition of the edited table (nothing is written): enabled,
// start, size and end, with what the change does to its data
class PartitionEditorScreen
{
  public:
    PartitionEditorScreen(const ScreenTools &tools, const EditorSources &sources);
    // Once the video mode is known
    void Configure(const Layout *layout);

    // A on start, size or end types a value, Y resets to the written table
    void Run(int part);
    // The number pad opens over the editor, which stays behind it: for the
    // dialogs' backdrop. Nothing while the number pad is closed.
    void DrawUnderNumberPad();

  private:
    // While the editor is open; the number pad draws it behind itself
    struct EditState
    {
        int part;
        int field; // EditField
        int step;  // Index of the Left/Right step
        FooterMessage message;
        bool field_available[EDIT_FIELD_COUNT];
        FLOAT bar_y;
        DWORD slide_tick;
        DpadRepeat field_repeat;
        DpadRepeat change_repeat;
    };

    // What Left/Right or A did on the selected field
    struct FieldInput
    {
        int change;     // The held step: -1, 0 between repeats, 1
        int horizontal; // Left -1, Right 1
        bool apply;     // A
    };

    // Where the panel's parts are, in layout space
    struct PanelGeometry
    {
        int left;
        int right;
        int top;
        int bottom;
        int first_item_y;
        int divider_y;
        int first_detail_y;
        int label_x;
        int value_x;
        int bar_height;
        int item_height;
    };

    // One read only line under the fields
    struct DetailRow
    {
        LPCWSTR label;
        wchar_t value[80];
        Font font;
        Color color;
    };

    // Running: one frame each; false to leave
    bool Step(EditState &state);
    void UpdateAvailableFields(EditState &state) const;
    bool HandleButtons(EditState &state);
    bool ChangeField(EditState &state, const FieldInput &input);
    void MoveField(EditState &state, int move);
    void ResetPartition(EditState &state);
    void ToggleEnabled(EditState &state);
    void MoveStart(EditState &state, int change);
    void ChangeSize(EditState &state, int change);
    void ChangeStep(EditState &state, int horizontal);
    void FillFreeSpace(EditState &state, bool apply);
    void Leave(int part);

    // Typing a value with the number pad, through the same functions as
    // Left/Right so it is aligned the same way. The message stays as it was
    // if the number pad is cancelled.
    void TypeValue(EditState &state);
    NumberInputRequest ValueRequest(int part, int field) const;
    // The footer message for what alignment changed, or NULL
    LPCWSTR ApplyTypedValue(int part, int field, ULONGLONG value);

    // Drawing, inside BeginFrame()/EndFrame()
    void DrawPanel(const EditState &state);
    void DrawSelectionBar(const EditState &state, const PanelGeometry &geometry);
    void DrawFields(const EditState &state, const PanelGeometry &geometry);
    void FormatFieldValue(const EditState &state, int field, wchar_t *text);
    void DrawDivider(const PanelGeometry &geometry);
    void DrawDetails(int part, const PanelGeometry &geometry);
    DetailRow NameDetail(int part) const;
    DetailRow ClusterDetail(int part) const;
    DetailRow OnDiskDetail(int part) const;
    DetailRow FreeSpaceDetail(int part) const;
    DetailRow StatusDetail(int part) const;
    void DrawFooter(const EditState &state, const PanelRect &box);
    PanelGeometry Geometry() const;
    int FieldTop(int field) const;
    int PanelTop() const;
    int PanelHeight() const;
    PanelRect PanelBox() const;
    const Theme &Colors() const
    {
        return m_ui.GetTheme();
    }

    Renderer &m_ui;
    GamepadInput &m_input;
    DialogPresenter &m_dialogs;
    PartitionTableSession &m_session;
    FilesystemCache &m_filesystems;
    const Settings &m_settings;
    NumberPad m_number_pad;
    const Layout *m_layout;
    // While the number pad is open: the editor behind it
    const EditState *m_number_pad_view;
};

#endif // PARTITION_EDITOR_SCREEN_H_
