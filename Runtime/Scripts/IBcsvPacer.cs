namespace BCSV
{
    /// <summary>
    /// A component that paces a <see cref="BcsvRecorder"/> on the same GameObject:
    /// it decides when a row is written, by calling <see cref="BcsvRecorder.Trigger"/>
    /// or <see cref="BcsvRecorder.Advance"/>, and when the file opens, by calling
    /// <see cref="BcsvRecorder.BeginRecording"/>.
    /// </summary>
    /// <remarks>
    /// A marker: it has no members, because the recorder needs to know only that
    /// somebody else is in charge. Its presence beside a recorder turns that
    /// recorder's own <c>FixedUpdate</c> pacing and its <c>recordOnStart</c> off,
    /// so a pacer never has to reach into the recorder's configuration to stop
    /// the two fighting, and the inspector shows no pacing choice at all - the
    /// GameObject's components are the choice.
    /// </remarks>
    public interface IBcsvPacer
    {
    }
}
