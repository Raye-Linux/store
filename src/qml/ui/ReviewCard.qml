// SPDX-License-Identifier: GPL-3.0-or-later
// One review: its stars and title, who wrote it and when, the text
// (folded after a few lines), and "Was this helpful?" with Report.
// The person's own review says so and can be deleted instead.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

Rectangle {
    id: card

    // a map from Reviews.reviews or Reviews.mine
    property var review: ({})
    property bool own: false
    // votes need a key the server gave this PC
    property bool canVote: true
    signal helpful(bool useful)
    signal reportRequested()
    signal deleteRequested()

    property bool expanded: false

    implicitHeight: column.implicitHeight + 2 * Theme.s6
    radius: Theme.radiusCard
    color: Theme.cardSolid
    border.width: 1
    border.color: own ? Qt.alpha(Theme.accentText, 0.4) : Theme.strokeSurface
    Accessible.role: Accessible.Grouping
    Accessible.name: review.summary || ""

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Stars { value: card.review.stars || 0; size: 12 }
            Text {
                visible: card.own
                text: i18nc("@info a label over the person's own review", "Your review")
                font: Theme.captionStrong
                color: Theme.accentText
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: card.review.otherLanguage === true
                text: i18nc("@info a review written in another language than the reader's", "Other language")
                font: Theme.caption
                color: Theme.textTertiary
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s1
            text: card.review.summary || ""
            textFormat: Text.PlainText
            font: Theme.bodyStrong
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: [card.review.author || i18nc("@info a review posted without a name", "Anonymous"),
                   Format.date(card.review.date)].filter(s => s !== "").join("  ·  ")
            textFormat: Text.PlainText
            font: Theme.caption
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Text {
            id: body
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            text: card.review.text || ""
            textFormat: Text.PlainText
            font: Theme.body
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            maximumLineCount: card.expanded ? 1000 : 5
            elide: Text.ElideRight
        }
        Button {
            visible: body.truncated || card.expanded
            subtle: true
            leftPadding: Theme.s2
            Layout.leftMargin: -Theme.s2
            implicitHeight: 26
            font: Theme.caption
            text: card.expanded ? i18nc("@action:button", "Show less") : i18nc("@action:button", "Read more")
            textColor: Theme.accentText
            onClicked: card.expanded = !card.expanded
        }

        // the votes line up at the bottom of cards side by side
        Item { Layout.fillHeight: true }
        // helpful, report; or delete for one's own
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            spacing: Theme.s2
            visible: !card.own
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: card.review.voted ? i18nc("@info after voting on a review", "Thanks for your feedback")
                                        : i18nc("@info", "Was this helpful?")
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            VoteButton {
                glyph: "thumb-up"
                count: card.review.up || 0
                Accessible.name: i18ncp("@action:button", "Helpful, one person said so", "Helpful, %1 people said so", count)
                onClicked: card.helpful(true)
            }
            VoteButton {
                glyph: "thumb-down"
                count: card.review.down || 0
                Accessible.name: i18ncp("@action:button", "Not helpful, one person said so", "Not helpful, %1 people said so", count)
                onClicked: card.helpful(false)
            }
            Button {
                id: reportButton
                subtle: true
                enabled: card.canVote
                glyph: "flag"
                glyphSize: 14
                implicitWidth: 28
                implicitHeight: 28
                textColor: enabled ? Theme.textSecondary : Theme.textDisabled
                Accessible.name: i18nc("@action:button report a review", "Report")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.delay: 600
                onClicked: card.reportRequested()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            visible: card.own
            Item { Layout.fillWidth: true }
            Button {
                visible: card.review.canDelete === true
                subtle: true
                implicitHeight: 28
                font: Theme.caption
                text: i18nc("@action:button the person's own review", "Delete")
                textColor: Theme.critical
                onClicked: card.deleteRequested()
            }
        }
    }

    component VoteButton: Button {
        property int count: 0
        subtle: !card.review.voted
        enabled: card.canVote && !card.review.voted
        glyphSize: 14
        implicitHeight: 28
        leftPadding: Theme.s3
        rightPadding: Theme.s3
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        font: Theme.caption
        text: Format.count(count)
        textColor: enabled ? Theme.text : Theme.textSecondary
    }
}
