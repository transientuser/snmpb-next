#include "graphdefinitioneditor.h"

#include "graphlabelresolver.h"
#include "graphrepository.h"

#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

GraphDefinitionEditor::GraphDefinitionEditor(const GraphDefinition &definition,
                                             const QList<AgentProfileRecord> &profiles,
                                             QWidget *parent)
    : QDialog(parent), working(definition), availableProfiles(profiles)
{
    setWindowTitle(tr("Graph Editor"));
    setModal(true);
    resize(620, 460);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    nameEdit = new QLineEdit(this); nameEdit->setObjectName("graphNameEdit");
    pollInterval = new QSpinBox(this); pollInterval->setRange(1, 86400);
    pollInterval->setSuffix(tr(" s")); pollInterval->setObjectName("graphPollInterval");
    historySize = new QSpinBox(this); historySize->setRange(1, 100000);
    historySize->setObjectName("graphHistorySize");
    form->addRow(tr("&Name:"), nameEdit);
    form->addRow(tr("&Poll interval:"), pollInterval);
    form->addRow(tr("&History / sample count:"), historySize);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Series"), this));
    seriesList = new QListWidget(this); seriesList->setObjectName("graphSeriesList");
    layout->addWidget(seriesList, 1);
    auto *seriesButtons = new QHBoxLayout;
    auto *add = new QPushButton(tr("New..."), this);
    editSeriesButton = new QPushButton(tr("Edit..."), this);
    deleteSeriesButton = new QPushButton(tr("Delete"), this);
    seriesButtons->addWidget(add); seriesButtons->addWidget(editSeriesButton);
    seriesButtons->addWidget(deleteSeriesButton); seriesButtons->addStretch();
    layout->addLayout(seriesButtons);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(add, &QPushButton::clicked, this, &GraphDefinitionEditor::addSeries);
    connect(editSeriesButton, &QPushButton::clicked, this, &GraphDefinitionEditor::editSeries);
    connect(deleteSeriesButton, &QPushButton::clicked, this, &GraphDefinitionEditor::deleteSeries);
    connect(seriesList, &QListWidget::itemDoubleClicked, this, [this] { editSeries(); });
    connect(seriesList, &QListWidget::currentItemChanged, this, &GraphDefinitionEditor::updateSeriesActions);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const GraphDefinition candidate = this->definition();
        if (candidate.name.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Graph Editor"), tr("Graph name is required."));
            nameEdit->setFocus(); return;
        }
        for (const auto &series : candidate.series)
            if (GraphRepository::canonicalOid(series.numericOid).isEmpty()) {
                QMessageBox::warning(this, tr("Graph Editor"), tr("Every series requires a numeric OID."));
                return;
            }
        working = candidate;
        accept();
    });
    setWorkingDefinition(definition);
}

void GraphDefinitionEditor::setWorkingDefinition(const GraphDefinition &definition)
{
    working = definition;
    nameEdit->setText(working.name);
    pollInterval->setValue(working.pollIntervalSeconds);
    historySize->setValue(working.maximumSamples);
    refreshSeries();
}

GraphDefinition GraphDefinitionEditor::definition() const
{
    GraphDefinition result = working;
    result.name = nameEdit->text().trimmed();
    result.pollIntervalSeconds = pollInterval->value();
    result.maximumSamples = historySize->value();
    return result;
}

void GraphDefinitionEditor::refreshSeries(const QString &selectedId)
{
    seriesList->clear();
    for (const auto &series : working.series) {
        const QString title = series.label.isEmpty() ? series.numericOid : series.label;
        auto *item = new QListWidgetItem(title, seriesList);
        item->setToolTip(series.numericOid);
        item->setData(Qt::UserRole, series.seriesId);
        if (series.seriesId == selectedId) seriesList->setCurrentItem(item);
    }
    if (!seriesList->currentItem() && seriesList->count()) seriesList->setCurrentRow(0);
    updateSeriesActions();
}

bool GraphDefinitionEditor::editSeriesDefinition(GraphSeriesDefinition *series)
{
    if (!series) return false;
    QDialog dialog(this); dialog.setWindowTitle(tr("Graph Series"));
    auto *form = new QFormLayout(&dialog);
    QComboBox profile, protocol, color, width, style;
    QLineEdit oid(series->numericOid), label(series->label);
    for (const auto &item : availableProfiles) profile.addItem(item.name, item.profileId);
    profile.setCurrentIndex(qMax(0, profile.findData(series->profileId)));
    protocol.addItem(QStringLiteral("SNMPv1"), 0); protocol.addItem(QStringLiteral("SNMPv2c"), 1);
    protocol.addItem(QStringLiteral("SNMPv3"), 2); protocol.setCurrentIndex(protocol.findData(series->protocol));
    const QStringList colors{tr("Blue"), tr("Red"), tr("Green"), tr("Magenta"), tr("Cyan"), tr("Yellow"), tr("Black"), tr("Gray")};
    for (int i = 0; i < colors.size(); ++i) color.addItem(colors.at(i), i);
    color.setCurrentIndex(qBound(0, series->color, colors.size() - 1));
    for (int i = 1; i <= 11; ++i) width.addItem(QString::number(i), i - 1);
    width.setCurrentIndex(qBound(0, series->width, 10));
    style.addItem(tr("Solid"), int(Qt::SolidLine)); style.addItem(tr("Dash"), int(Qt::DashLine));
    style.addItem(tr("Dot"), int(Qt::DotLine)); style.addItem(tr("Dash dot"), int(Qt::DashDotLine));
    style.addItem(tr("Dash dot dot"), int(Qt::DashDotDotLine));
    const int styleIndex = style.findData(series->style); style.setCurrentIndex(styleIndex < 0 ? 0 : styleIndex);
    form->addRow(tr("Agent &Profile:"), &profile); form->addRow(tr("&Protocol:"), &protocol);
    form->addRow(tr("Numeric &OID:"), &oid); form->addRow(tr("&Label:"), &label);
    form->addRow(tr("&Color:"), &color); form->addRow(tr("Line &width:"), &width);
    form->addRow(tr("Line &style:"), &style);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (GraphRepository::canonicalOid(oid.text()).isEmpty() || profile.currentIndex() < 0) {
            QMessageBox::warning(&dialog, tr("Graph Series"), tr("Select an Agent Profile and enter a numeric OID."));
            return;
        }
        const auto selected = std::find_if(availableProfiles.cbegin(), availableProfiles.cend(),
            [&profile](const AgentProfileRecord &item) { return item.profileId == profile.currentData().toString(); });
        const int selectedProtocol = protocol.currentData().toInt();
        if (selected == availableProfiles.cend() ||
            (selectedProtocol == 0 && !selected->v1) ||
            (selectedProtocol == 1 && !selected->v2) ||
            (selectedProtocol == 2 && !selected->v3)) {
            QMessageBox::warning(&dialog, tr("Graph Series"), tr("The selected protocol is not enabled for this Agent Profile."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    series->profileId = profile.currentData().toString(); series->legacyProfileName = profile.currentText();
    series->protocol = protocol.currentData().toInt();
    series->numericOid = GraphRepository::canonicalOid(oid.text());
    series->label = label.text().trimmed();
    if (series->label.isEmpty()) series->label = GraphLabelResolver::displayLabel(series->numericOid);
    series->color = color.currentData().toInt(); series->width = width.currentData().toInt();
    series->style = style.currentData().toInt();
    return true;
}

void GraphDefinitionEditor::addSeries()
{
    GraphSeriesDefinition series; series.seriesId = GraphRepository::createId();
    if (editSeriesDefinition(&series)) { working.series.append(series); refreshSeries(series.seriesId); }
}

void GraphDefinitionEditor::editSeries()
{
    if (!seriesList->currentItem()) return;
    const QString id = seriesList->currentItem()->data(Qt::UserRole).toString();
    for (auto &series : working.series)
        if (series.seriesId == id && editSeriesDefinition(&series)) { refreshSeries(id); return; }
}

void GraphDefinitionEditor::deleteSeries()
{
    if (!seriesList->currentItem()) return;
    const QString id = seriesList->currentItem()->data(Qt::UserRole).toString();
    for (int i = 0; i < working.series.size(); ++i)
        if (working.series.at(i).seriesId == id) { working.series.removeAt(i); break; }
    refreshSeries();
}

void GraphDefinitionEditor::updateSeriesActions()
{
    const bool selected = seriesList->currentItem();
    editSeriesButton->setEnabled(selected); deleteSeriesButton->setEnabled(selected);
}
