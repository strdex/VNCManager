#pragma once

#include "ConnectionProfile.h"

#include <QMainWindow>
#include <QList>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QTabWidget;
class QScrollArea;
class QLabel;
class VncView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewConnection();
    void onEditConnection();
    void onRenameConnection();
    void onDeleteConnection();
    void onExportConnections();
    void onImportConnections();
    void onConnectCurrent();
    void onProfileDoubleClicked();
    void onQuickConnect();
    void onListContextMenu(const QPoint& pos);
    void onToggleFullScreen();
    void onFitWindow();
    void onActualSize();
    void onZoomIn();
    void onZoomOut();
    void onDisconnectActive();
    void onSendCtrlAltDelete();
    void onSendWindowsKey();
    void onViewOnlyToggled(bool checked);
    void onRemoteCursorToggled(bool checked);
    void onShowConnectionInfo();
    void onFileTransfer();
    void onPasteToRemote();
    void onCopyFromRemote();
    void onCloseActiveTab();
    void onTabChanged(int index);

private:
    void buildUi();
    void loadProfiles();
    void saveProfiles() const;
    void refreshList();

    ConnectionProfile profileById(const QString& id) const;
    void updateProfileInList(const ConnectionProfile& profile);
    // listede (grup cocuklari arasinda) verilen kimlige sahip dugumu bulur
    QTreeWidgetItem* findProfileItem(const QString& id) const;
    // secili dugum bir sunucuysa kimligini, grup dugumu/bos secim ise bos dondurur
    QString selectedProfileId() const;
    // secimi verilen kimlikteki sunucuya tasir; bulunamazsa false dondurur
    bool selectProfile(const QString& id);
    // Basarili baglantida kayit disindan gelen profili listeye ekler.
    void addProfileToList(const ConnectionProfile& profile);
    void connectProfile(ConnectionProfile profile);
    VncView* viewForWidget(QWidget* widget) const;
    VncView* activeView() const;
    QScrollArea* wrapView(VncView* view, ScalingMode mode) const;
    void applyScaling(VncView* view, ScalingMode mode);
    void updateScalingActions();
    void updateZoomLabel();

    QList<ConnectionProfile> m_profiles;

    QTreeWidget* m_list = nullptr;
    QLineEdit* m_quickHostEdit = nullptr;
    QTabWidget* m_tabs = nullptr;

    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QAction* m_transferAction = nullptr;
    QAction* m_cadAction = nullptr;
    QAction* m_winAction = nullptr;
    QAction* m_viewOnlyAction = nullptr;
    QAction* m_cursorAction = nullptr;
    QAction* m_editAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_fullScreenAction = nullptr;
    QAction* m_infoAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_closeTabAction = nullptr;
    QLabel* m_zoomLabel = nullptr;
};
