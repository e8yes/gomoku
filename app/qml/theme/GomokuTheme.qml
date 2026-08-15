import QtQuick

pragma Singleton

QtObject {
    // Theme Colors
    readonly property color background: "#181A1F"
    readonly property color surfaceDark: "#21252B"
    readonly property color surfaceLight: "#282C34"
    readonly property color surfaceHover: "#323842"
    readonly property color border: "#3E4451"
    readonly property color borderActive: "#4C5363"

    // Board & Wood Styling
    readonly property color boardBackground: "#E0B376"
    readonly property color boardGridLine: "#4A3319"
    readonly property color boardBorder: "#34220F"
    readonly property color starPoint: "#3A2613"

    // Stones
    readonly property color blackStone: "#1E1E24"
    readonly property color blackStoneSpecular: "#4E4E58"
    readonly property color blackStoneText: "#FFFFFF"
    
    readonly property color whiteStone: "#F5F5F7"
    readonly property color whiteStoneShadow: "#D0D0D5"
    readonly property color whiteStoneText: "#1A1A1E"

    readonly property color accentLatestMove: "#E06C75"
    readonly property color winningHighlight: "#98C379"

    // Typography Colors
    readonly property color textPrimary: "#ECEFF4"
    readonly property color textSecondary: "#ABB2BF"
    readonly property color textMuted: "#5C6370"
    readonly property color textAccent: "#61AFEF"
    readonly property color textSuccess: "#98C379"
    readonly property color textDanger: "#E06C75"
    readonly property color textWarning: "#E5C07B"

    // Typography Sizing
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeRegular: 14
    readonly property int fontSizeMedium: 16
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 22
    readonly property int fontSizeHeader: 28

    readonly property string fontFamily: "system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif"

    // Spacing
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
}
